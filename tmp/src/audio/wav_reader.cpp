#include "audio/wav_reader.hpp"

#include <cstring>
#include <iostream>

namespace i2c_audio {

namespace {

uint32_t ReadLE32(std::ifstream& f) {
    uint8_t b[4] = {0, 0, 0, 0};
    f.read(reinterpret_cast<char*>(b), 4);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
}

uint16_t ReadLE16(std::ifstream& f) {
    uint8_t b[2] = {0, 0};
    f.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
}

}  // namespace

WavReader::WavReader(const std::string& path)
    : file_(path, std::ios::binary) {
    if (!file_.is_open()) {
        std::cerr << "WavReader: no se pudo abrir '" << path << "'\n";
        return;
    }
    valid_ = ParseHeader();
}

bool WavReader::ParseHeader() {
    char tag[4];

    file_.read(tag, 4);
    if (file_.gcount() != 4 || std::strncmp(tag, "RIFF", 4) != 0) {
        std::cerr << "WavReader: no es un archivo RIFF válido\n";
        return false;
    }
    ReadLE32(file_);  // tamaño total del RIFF, no lo necesitamos

    file_.read(tag, 4);
    if (std::strncmp(tag, "WAVE", 4) != 0) {
        std::cerr << "WavReader: no es un archivo WAVE válido\n";
        return false;
    }

    // Recorre chunks hasta encontrar "fmt " y "data".
    bool have_fmt = false, have_data = false;
    while (file_ && !(have_fmt && have_data)) {
        char chunk_id[4];
        file_.read(chunk_id, 4);
        if (file_.gcount() != 4) break;
        uint32_t chunk_size = ReadLE32(file_);

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t audio_format = ReadLE16(file_);
            num_channels_ = ReadLE16(file_);
            sample_rate_ = ReadLE32(file_);
            ReadLE32(file_);  // byte rate
            ReadLE16(file_);  // block align
            bits_per_sample_ = ReadLE16(file_);
            if (audio_format != 1) {
                std::cerr << "WavReader: sólo se soporta PCM entero "
                           << "(audio_format=1), encontrado=" << audio_format
                           << "\n";
                return false;
            }
            // Saltar bytes extra del chunk fmt si los hubiera.
            if (chunk_size > 16) file_.seekg(chunk_size - 16, std::ios::cur);
            have_fmt = true;
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            data_size_bytes_ = chunk_size;
            data_bytes_remaining_ = chunk_size;
            have_data = true;
            // Dejamos el cursor justo al inicio de los datos de audio.
        } else {
            // Chunk desconocido (p.ej. "LIST"): lo saltamos.
            file_.seekg(chunk_size, std::ios::cur);
        }
    }

    if (!have_fmt || !have_data) {
        std::cerr << "WavReader: faltan chunks 'fmt ' o 'data'\n";
        return false;
    }
    return true;
}

size_t WavReader::ReadBlock(uint8_t* buffer, size_t max_bytes) {
    if (!valid_ || data_bytes_remaining_ == 0) return 0;
    const size_t to_read =
        std::min<size_t>(max_bytes, data_bytes_remaining_);
    file_.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(to_read));
    const size_t got = static_cast<size_t>(file_.gcount());
    data_bytes_remaining_ -= static_cast<uint32_t>(got);
    return got;
}

}  // namespace i2c_audio
