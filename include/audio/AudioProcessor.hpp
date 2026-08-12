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

    // Adds `frameCount` frames. When `useLeft` is true only the left channel
    // is accumulated, otherwise only the right channel (single-microphone
    // setups where the mic transmits on the channel chosen by its L/R pin).
    void addFrames(const AudioFrame* frames, size_t frameCount, bool useLeft);

    // RMS level in dBFS (silence => -inf, full scale => 0 dB).
    double rmsDb() const;
    double peakDb() const;
    bool hasData() const { return sampleCount_ != 0; }

private:
    double sumSquares_ = 0.0;
    double peakAbs_ = 0.0;
    uint64_t sampleCount_ = 0;
};

// One-pole high-pass filter (DC blocker) with an adjustable cutoff, applied to
// 16-bit samples BEFORE the digital gain. Removes the INMP441's DC offset and
// the sub-bass hum of the power rail, which the gain would otherwise amplify
// into clipping/saturation. A cutoff of 0 Hz disables the filter (bypass).
// Header-only so the host unit tests (which link only the test translation
// unit, see Makefile `test` target) can exercise it without extra link steps.
class HighPassFilter {
public:
    // Cutoff in Hz (0 = bypass). `sampleRate` must be > 0 for the filter to
    // engage. Resets the internal state on every call.
    void setCutoffHz(double hz, uint32_t sampleRate) {
        if (hz <= 0.0 || sampleRate == 0) {
            enabled_ = false;
            coeffR_ = 0.0;
        } else {
            enabled_ = true;
            // R = 1 - 2*pi*fc/fs; stable for fc < fs/pi, well within our
            // clamped range (0..1000 Hz at 8..48 kHz sample rates).
            coeffR_ = 1.0 - 2.0 * 3.14159265358979323846 * hz /
                      static_cast<double>(sampleRate);
        }
        reset();
    }

    bool enabled() const { return enabled_; }

    void reset() {
        prevX_ = 0.0;
        prevY_ = 0.0;
    }

    // One-pole high-pass: y[n] = x[n] - x[n-1] + R*y[n-1], on full-scale
    // 16-bit samples. Returns the filtered sample (unchanged when disabled).
    int16_t process(int16_t sample) {
        if (!enabled_) {
            return sample;
        }
        const double x = static_cast<double>(sample) / 32768.0;
        const double y = x - prevX_ + coeffR_ * prevY_;
        prevX_ = x;
        prevY_ = y;
        long v = static_cast<long>(y * 32768.0);
        if (v > 32767L) {
            v = 32767L;
        } else if (v < -32768L) {
            v = -32768L;
        }
        return static_cast<int16_t>(v);
    }

private:
    double coeffR_ = 0.0;
    double prevX_ = 0.0;
    double prevY_ = 0.0;
    bool enabled_ = false;
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
