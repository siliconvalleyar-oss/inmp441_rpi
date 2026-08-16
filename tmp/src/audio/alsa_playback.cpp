#include "audio/alsa_playback.hpp"

#include <alsa/asoundlib.h>

#include <iostream>
#include <vector>

namespace i2c_audio {

AlsaPlayback::AlsaPlayback(std::string device_name)
    : device_name_(std::move(device_name)) {}

AlsaPlayback::~AlsaPlayback() { Close(); }

bool AlsaPlayback::Open(const WavReader& wav) {
    int err = snd_pcm_open(&pcm_handle_, device_name_.c_str(),
                             SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cerr << "AlsaPlayback: snd_pcm_open('" << device_name_
                   << "') falló: " << snd_strerror(err) << "\n";
        pcm_handle_ = nullptr;
        return false;
    }

    snd_pcm_format_t format;
    switch (wav.bits_per_sample()) {
        case 16: format = SND_PCM_FORMAT_S16_LE; break;
        case 24: format = SND_PCM_FORMAT_S24_LE; break;
        case 32: format = SND_PCM_FORMAT_S32_LE; break;
        default:
            std::cerr << "AlsaPlayback: bits_per_sample no soportado: "
                       << wav.bits_per_sample() << "\n";
            Close();
            return false;
    }

    err = snd_pcm_set_params(
        pcm_handle_, format, SND_PCM_ACCESS_RW_INTERLEAVED,
        wav.num_channels(), wav.sample_rate(),
        1 /* permitir resampleo por software si el HW no soporta el rate */,
        200000 /* latencia objetivo en us */);
    if (err < 0) {
        std::cerr << "AlsaPlayback: snd_pcm_set_params falló: "
                   << snd_strerror(err) << "\n";
        Close();
        return false;
    }

    return true;
}

bool AlsaPlayback::PlayAll(WavReader& wav) {
    if (!pcm_handle_) {
        std::cerr << "AlsaPlayback: dispositivo no abierto\n";
        return false;
    }

    const size_t bytes_per_frame =
        wav.num_channels() * (wav.bits_per_sample() / 8);
    const snd_pcm_uframes_t period_frames = 1024;
    std::vector<uint8_t> buffer(period_frames * bytes_per_frame);

    size_t bytes_read;
    while ((bytes_read = wav.ReadBlock(buffer.data(), buffer.size())) > 0) {
        const snd_pcm_uframes_t frames = bytes_read / bytes_per_frame;
        snd_pcm_sframes_t written =
            snd_pcm_writei(pcm_handle_, buffer.data(), frames);

        if (written == -EPIPE) {
            std::cerr << "AlsaPlayback: underrun, recuperando...\n";
            snd_pcm_prepare(pcm_handle_);
            continue;
        } else if (written < 0) {
            written = snd_pcm_recover(pcm_handle_,
                                         static_cast<int>(written), 0);
            if (written < 0) {
                std::cerr << "AlsaPlayback: error irrecuperable: "
                           << snd_strerror(static_cast<int>(written)) << "\n";
                return false;
            }
        }
    }

    snd_pcm_drain(pcm_handle_);
    return true;
}

void AlsaPlayback::Close() {
    if (pcm_handle_) {
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
}

}  // namespace i2c_audio
