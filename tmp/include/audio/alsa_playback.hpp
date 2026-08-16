#pragma once

#include <cstdint>
#include <string>

#include "wav_reader.hpp"

typedef struct _snd_pcm snd_pcm_t;

namespace i2c_audio {

// Reproduce un WavReader ya abierto por la salida de audio ALSA
// (parlante/jack/HDMI/USB, según el dispositivo por defecto de la Pi).
class AlsaPlayback {
public:
    explicit AlsaPlayback(std::string device_name = "default");
    ~AlsaPlayback();

    AlsaPlayback(const AlsaPlayback&) = delete;
    AlsaPlayback& operator=(const AlsaPlayback&) = delete;

    // Abre el dispositivo configurado con el formato del WavReader dado.
    bool Open(const WavReader& wav);
    void Close();

    // Reproduce todo el contenido restante del WavReader.
    bool PlayAll(WavReader& wav);

private:
    std::string device_name_;
    snd_pcm_t* pcm_handle_ = nullptr;
};

}  // namespace i2c_audio
