#include "audio/wav_writer.hpp"

#include <cstring>
#include <iostream>

namespace i2c_audio {

namespace {

void WriteLE32(std::ofstream& f, uint32_t v) {
    uint8_t b[4] = {static_cast<uint8_t>(v & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF),
                     static_cast<uint8_t>((v >> 16) & 0xFF),
                     static_cast<uint8_t>((v >> 24) & 0xFF)};
    f.write(reinterpret_cast<const char*>(b), 4);
}

void WriteLE16(std::ofstream& f, uint16_t v) {
    uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF)};
    f.write(reinterpret_cast<const char*>(b), 2);
}

}  // namespace

WavWriter::WavWriter(const std::string& path,
                       uint32_t sample_rate,
                       uint16_t bits_per_sample,
                       uint16_t num_channels)
    : file_(path, std::ios::binary | std::ios::trunc),
      sample_rate_(sample_rate),
      bits_per_sample_(bits_per_sample),
      num_channels_(num_channels) {
    if (!file_.is_open()) {
        std::cerr << "WavWriter: no se pudo abrir '" << path << "'\n";
        return;
    }
    WriteHeaderPlaceholder();
}

WavWriter::~WavWriter() {
    if (!finalized_) Finalize();
}

bool WavWriter::IsOpen() const { return file_.is_open(); }

void WavWriter::WriteHeaderPlaceholder() {
    // Cabecera WAV/RIFF de 44 bytes, PCM.
    file_.write("RIFF", 4);
    WriteLE32(file_, 0);  // tamaño total, se corrige en Finalize()
    file_.write("WAVE", 4);

    file_.write("fmt ", 4);
    WriteLE32(file_, 16);  // tamaño del bloque fmt
    WriteLE16(file_, 1);   // PCM entero
    WriteLE16(file_, num_channels_);
    WriteLE32(file_, sample_rate_);
    const uint32_t byte_rate =
        sample_rate_ * num_channels_ * (bits_per_sample_ / 8);
    WriteLE32(file_, byte_rate);
    const uint16_t block_align =
        static_cast<uint16_t>(num_channels_ * (bits_per_sample_ / 8));
    WriteLE16(file_, block_align);
    WriteLE16(file_, bits_per_sample_);

    file_.write("data", 4);
    WriteLE32(file_, 0);  // tamaño de datos, se corrige en Finalize()
}

bool WavWriter::Write(const uint8_t* data, size_t bytes) {
    if (!file_.is_open()) return false;
    file_.write(reinterpret_cast<const char*>(data), bytes);
    if (!file_) return false;
    data_bytes_written_ += bytes;
    return true;
}

void WavWriter::PatchHeaderSizes() {
    if (!file_.is_open()) return;
    const uint32_t riff_size =
        static_cast<uint32_t>(36 + data_bytes_written_);
    const uint32_t data_size = static_cast<uint32_t>(data_bytes_written_);

    file_.flush();
    file_.seekp(4, std::ios::beg);
    WriteLE32(file_, riff_size);
    file_.seekp(40, std::ios::beg);
    WriteLE32(file_, data_size);
    file_.flush();
}

void WavWriter::Finalize() {
    if (finalized_) return;
    PatchHeaderSizes();
    if (file_.is_open()) file_.close();
    finalized_ = true;
}

}  // namespace i2c_audio
