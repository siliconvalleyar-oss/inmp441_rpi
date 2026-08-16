#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace i2c_audio {

// Escribe un archivo WAV PCM (16/24/32 bit, mono o estereo) de forma
// incremental: primero reserva la cabecera, va volcando bloques de audio
// con Write(), y al final Finalize() corrige los tamaños en la cabecera.
class WavWriter {
public:
    WavWriter(const std::string& path,
               uint32_t sample_rate,
               uint16_t bits_per_sample,
               uint16_t num_channels);
    ~WavWriter();

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool IsOpen() const;

    // Escribe bytes de audio PCM crudo (ya en el formato/endianness final).
    bool Write(const uint8_t* data, size_t bytes);

    // Cierra el archivo, corrigiendo los campos de tamaño en la cabecera WAV.
    void Finalize();

private:
    void WriteHeaderPlaceholder();
    void PatchHeaderSizes();

    std::ofstream file_;
    uint32_t sample_rate_;
    uint16_t bits_per_sample_;
    uint16_t num_channels_;
    uint64_t data_bytes_written_ = 0;
    bool finalized_ = false;
};

}  // namespace i2c_audio
