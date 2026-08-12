#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "audio/SampleFormat.hpp"

namespace audio {

// Accumulates RMS / peak statistics over a stream of frames.
class RmsAnalyzer {
public:
    void reset();

    // Adds `frameCount` frames. When `useLeftOnly` is true only the left
    // channel is accumulated (single-microphone setups).
    void addFrames(const AudioFrame* frames, size_t frameCount, bool useLeftOnly);

    // RMS level in dBFS (silence => -inf, full scale => 0 dB).
    double rmsDb() const;
    double peakDb() const;
    bool hasData() const { return sampleCount_ != 0; }

private:
    double sumSquares_ = 0.0;
    double peakAbs_ = 0.0;
    uint64_t sampleCount_ = 0;
};

// Minimal RIFF/WAVE writer for 16-bit PCM data. The header sizes are patched
// on close so streams can be written incrementally.
class WaveWriter {
public:
    WaveWriter(std::string path, uint32_t sampleRate, bool stereo);
    ~WaveWriter();

    WaveWriter(const WaveWriter&) = delete;
    WaveWriter& operator=(const WaveWriter&) = delete;

    bool open();
    bool writeFrames16(const int16_t* interleaved, size_t frameCount);
    bool close();
    bool isOpen() const { return file_ != nullptr; }
    uint64_t framesWritten() const { return framesWritten_; }

private:
    void writeHeader();
    bool finalizeHeader();

    std::string path_;
    uint32_t sampleRate_ = 48000;
    uint16_t channels_ = 1;
    uint32_t blockAlign_ = 2;
    uint32_t byteRate_ = 96000;
    void* file_ = nullptr;  // FILE*, kept opaque to avoid <cstdio> in the header
    uint64_t framesWritten_ = 0;
};

// Renders an ASCII level meter (e.g. "[#######-----] -12.3 dBFS").
std::string renderMeter(double rmsDb, double peakDb, int width);

}  // namespace audio
