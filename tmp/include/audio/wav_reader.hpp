#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace i2c_audio {

// Lee un archivo WAV PCM simple (formato 1, sin chunks extra antes de
// "data") y permite ir extrayendo bloques de audio crudo para reproducir.
class WavReader {
public:
    explicit WavReader(const std::string& path);
    ~WavReader() = default;

    WavReader(const WavReader&) = delete;
    WavReader& operator=(const WavReader&) = delete;

    // true si el archivo se abrió y la cabecera es un WAV PCM válido.
    bool IsValid() const { return valid_; }

    uint32_t sample_rate() const { return sample_rate_; }
    uint16_t bits_per_sample() const { return bits_per_sample_; }
    uint16_t num_channels() const { return num_channels_; }
    uint32_t data_size_bytes() const { return data_size_bytes_; }

    // Lee hasta max_bytes de audio PCM crudo. Devuelve los bytes
    // efectivamente leídos (0 = fin de datos).
    size_t ReadBlock(uint8_t* buffer, size_t max_bytes);

private:
    bool ParseHeader();

    std::ifstream file_;
    bool valid_ = false;
    uint32_t sample_rate_ = 0;
    uint16_t bits_per_sample_ = 0;
    uint16_t num_channels_ = 0;
    uint32_t data_size_bytes_ = 0;
    uint32_t data_bytes_remaining_ = 0;
};

}  // namespace i2c_audio
