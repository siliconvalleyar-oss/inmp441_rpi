#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Forward declaration de ALSA
typedef struct _snd_pcm snd_pcm_t;

namespace i2c_audio {

struct CaptureConfig {
    std::string device_name = "default";  // p.ej. "hw:0,0" o "plughw:0,0"
    uint32_t sample_rate = 48000;
    uint16_t bits_per_sample = 32;  // ancho del S32_LE nativo capturado (el
                                     // INMP441 entrega 24 bits útiles dentro
                                     // de tramas I2S de 32 bits; la conversión
                                     // al ancho de salida la hace el cliente)
    uint16_t num_channels = 1;
    uint32_t period_frames = 1024;  // tamaño de bloque de lectura
};

// Callback invocado con cada bloque de audio PCM crudo capturado.
// Devuelve false para pedir que se detenga la captura.
using AudioBlockCallback =
    std::function<bool(const uint8_t* data, size_t bytes)>;

// Envuelve el manejo de ALSA para capturar desde la tarjeta I2S expuesta
// por el overlay dtoverlay=inmp441-bare (u otro compatible) en el kernel
// de Raspberry Pi OS.
class AlsaCapture {
public:
    explicit AlsaCapture(CaptureConfig config);
    ~AlsaCapture();

    AlsaCapture(const AlsaCapture&) = delete;
    AlsaCapture& operator=(const AlsaCapture&) = delete;

    bool Open();
    void Close();

    // Captura en bucle hasta que on_block devuelva false o se pida detener
    // externamente vía RequestStop().
    bool CaptureLoop(const AudioBlockCallback& on_block);

    void RequestStop() { stop_requested_ = true; }

    // Cantidad de overruns (xrun) detectados durante la última captura.
    // Un overrun significa muestras perdidas: la duración efectiva del
    // archivo puede ser menor a la solicitada.
    unsigned int xrun_count() const { return xrun_count_; }

    const CaptureConfig& config() const { return config_; }

private:
    bool ConfigureHardwareParams();

    CaptureConfig config_;
    snd_pcm_t* pcm_handle_ = nullptr;
    bool stop_requested_ = false;
    unsigned int xrun_count_ = 0;
};

}  // namespace i2c_audio
