#include "audio/alsa_capture.hpp"

#include <alsa/asoundlib.h>

#include <iostream>
#include <vector>

namespace i2c_audio {

AlsaCapture::AlsaCapture(CaptureConfig config) : config_(std::move(config)) {}

AlsaCapture::~AlsaCapture() { Close(); }

bool AlsaCapture::Open() {
    int err = snd_pcm_open(&pcm_handle_, config_.device_name.c_str(),
                             SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        std::cerr << "AlsaCapture: snd_pcm_open('" << config_.device_name
                   << "') falló: " << snd_strerror(err) << "\n";
        pcm_handle_ = nullptr;
        return false;
    }

    if (!ConfigureHardwareParams()) {
        Close();
        return false;
    }
    return true;
}

bool AlsaCapture::ConfigureHardwareParams() {
    snd_pcm_hw_params_t* hw_params = nullptr;
    snd_pcm_hw_params_alloca(&hw_params);

    int err = snd_pcm_hw_params_any(pcm_handle_, hw_params);
    if (err < 0) {
        std::cerr << "AlsaCapture: hw_params_any falló: "
                   << snd_strerror(err) << "\n";
        return false;
    }

    snd_pcm_hw_params_set_access(pcm_handle_, hw_params,
                                    SND_PCM_ACCESS_RW_INTERLEAVED);

    // El INMP441 entrega 24 bits útiles dentro de slots I2S de 32 bits,
    // alineados a MSB (el byte bajo es relleno). Por eso siempre se captura
    // como S32_LE nativo y la conversión a 16/24/32 bits de salida la hace
    // el consumidor (ver main.cpp), sin depender de conversiones de la capa
    // plug de ALSA.
    const snd_pcm_format_t format = SND_PCM_FORMAT_S32_LE;
    err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, format);
    if (err < 0) {
        std::cerr << "AlsaCapture: formato no soportado por el driver: "
                   << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_hw_params_set_channels(pcm_handle_, hw_params,
                                            config_.num_channels);
    if (err < 0) {
        std::cerr << "AlsaCapture: canales no soportados: "
                   << snd_strerror(err) << "\n";
        return false;
    }

    unsigned int rate = config_.sample_rate;
    err = snd_pcm_hw_params_set_rate_near(pcm_handle_, hw_params, &rate, 0);
    if (err < 0) {
        std::cerr << "AlsaCapture: sample_rate no soportado: "
                   << snd_strerror(err) << "\n";
        return false;
    }
    if (rate != config_.sample_rate) {
        std::cerr << "AlsaCapture: aviso, el driver ajustó el sample rate a "
                   << rate << " Hz (pedido: " << config_.sample_rate
                   << " Hz)\n";
        config_.sample_rate = rate;
    }

    snd_pcm_uframes_t period_size = config_.period_frames;
    err = snd_pcm_hw_params_set_period_size_near(pcm_handle_, hw_params,
                                                     &period_size, nullptr);
    if (err < 0) {
        std::cerr << "AlsaCapture: aviso, no se pudo fijar period_size: "
                   << snd_strerror(err) << "\n";
    } else {
        config_.period_frames = static_cast<uint32_t>(period_size);
    }

    err = snd_pcm_hw_params(pcm_handle_, hw_params);
    if (err < 0) {
        std::cerr << "AlsaCapture: snd_pcm_hw_params falló: "
                   << snd_strerror(err) << "\n";
        return false;
    }

    err = snd_pcm_prepare(pcm_handle_);
    if (err < 0) {
        std::cerr << "AlsaCapture: snd_pcm_prepare falló: "
                   << snd_strerror(err) << "\n";
        return false;
    }

    return true;
}

bool AlsaCapture::CaptureLoop(const AudioBlockCallback& on_block) {
    if (!pcm_handle_) {
        std::cerr << "AlsaCapture: dispositivo no abierto\n";
        return false;
    }

    const size_t bytes_per_frame =
        config_.num_channels * (config_.bits_per_sample / 8);
    std::vector<uint8_t> buffer(config_.period_frames * bytes_per_frame);

    stop_requested_ = false;
    while (!stop_requested_) {
        snd_pcm_sframes_t frames_read = snd_pcm_readi(
            pcm_handle_, buffer.data(), config_.period_frames);

        if (frames_read == -EPIPE) {
            // Overrun: el buffer del kernel se llenó porque no leímos a
            // tiempo, se perdieron muestras. Se cuenta para informar al
            // final (la duración efectiva puede quedar corta) y se
            // recupera continuando.
            ++xrun_count_;
            std::cerr << "AlsaCapture: overrun (xrun) #" << xrun_count_
                       << ", recuperando...\n";
            snd_pcm_prepare(pcm_handle_);
            continue;
        } else if (frames_read < 0) {
            frames_read = snd_pcm_recover(pcm_handle_,
                                             static_cast<int>(frames_read), 0);
            if (frames_read < 0) {
                std::cerr << "AlsaCapture: error de lectura irrecuperable: "
                           << snd_strerror(static_cast<int>(frames_read))
                           << "\n";
                return false;
            }
            continue;
        }

        const size_t bytes_read =
            static_cast<size_t>(frames_read) * bytes_per_frame;
        if (bytes_read > 0 && on_block) {
            if (!on_block(buffer.data(), bytes_read)) break;
        }
    }
    return true;
}

void AlsaCapture::Close() {
    if (pcm_handle_) {
        snd_pcm_drain(pcm_handle_);
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
}

}  // namespace i2c_audio
