#include "audio/AudioProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Logger.hpp"

namespace audio {

// ---------------------------------------------------------------------------
// RmsAnalyzer
// ---------------------------------------------------------------------------

void RmsAnalyzer::reset() {
    sumSquares_ = 0.0;
    peakAbs_ = 0.0;
    sampleCount_ = 0;
}

void RmsAnalyzer::addFrames(const AudioFrame* frames, size_t frameCount, bool useLeftOnly) {
    for (size_t i = 0; i < frameCount; ++i) {
        const float left = frames[i].leftFloat();
        const float right = frames[i].rightFloat();

        if (useLeftOnly) {
            const double sample = left;
            sumSquares_ += sample * sample;
            peakAbs_ = std::max(peakAbs_, std::fabs(sample));
            ++sampleCount_;
        } else {
            const double l = left;
            const double r = right;
            sumSquares_ += l * l + r * r;
            peakAbs_ = std::max(peakAbs_, std::max(std::fabs(l), std::fabs(r)));
            sampleCount_ += 2;
        }
    }
}

double RmsAnalyzer::rmsDb() const {
    if (sampleCount_ == 0) {
        return -120.0;
    }
    const double rms = std::sqrt(sumSquares_ / static_cast<double>(sampleCount_));
    if (rms <= 1e-9) {
        return -120.0;
    }
    return 20.0 * std::log10(rms);
}

double RmsAnalyzer::peakDb() const {
    if (peakAbs_ <= 1e-9) {
        return -120.0;
    }
    return 20.0 * std::log10(peakAbs_);
}

// ---------------------------------------------------------------------------
// WaveWriter
// ---------------------------------------------------------------------------

namespace {
constexpr size_t kRiffHeaderSize = 44;
}  // namespace

WaveWriter::WaveWriter(std::string path, uint32_t sampleRate, bool stereo)
    : path_(std::move(path)),
      sampleRate_(sampleRate),
      channels_(stereo ? 2 : 1) {
    const uint16_t bitsPerSample = 16;
    blockAlign_ = static_cast<uint32_t>(channels_) * (bitsPerSample / 8);
    byteRate_ = sampleRate_ * blockAlign_;
}

WaveWriter::~WaveWriter() {
    close();
}

bool WaveWriter::open() {
    if (file_ != nullptr) {
        return true;
    }
    std::FILE* file = std::fopen(path_.c_str(), "wb");
    if (file == nullptr) {
        core::Logger::instance().error("cannot open WAV file '%s' for writing",
                                       path_.c_str());
        return false;
    }
    file_ = file;
    framesWritten_ = 0;
    writeHeader();
    return true;
}

void WaveWriter::writeHeader() {
    std::FILE* file = static_cast<std::FILE*>(file_);

    // Placeholder sizes; patched on close.
    const uint32_t dataBytes = 0;

    std::fwrite("RIFF", 1, 4, file);
    std::fwrite(&dataBytes, 4, 1, file);  // patched later (36 + data)
    std::fwrite("WAVE", 1, 4, file);

    std::fwrite("fmt ", 1, 4, file);
    const uint32_t fmtSize = 16;
    const uint16_t audioFormat = 1;  // PCM
    std::fwrite(&fmtSize, 4, 1, file);
    std::fwrite(&audioFormat, 2, 1, file);
    std::fwrite(&channels_, 2, 1, file);
    std::fwrite(&sampleRate_, 4, 1, file);
    std::fwrite(&byteRate_, 4, 1, file);
    std::fwrite(&blockAlign_, 2, 1, file);
    const uint16_t bitsPerSample = 16;
    std::fwrite(&bitsPerSample, 2, 1, file);

    std::fwrite("data", 1, 4, file);
    std::fwrite(&dataBytes, 4, 1, file);  // patched on close
}

bool WaveWriter::writeFrames16(const int16_t* interleaved, size_t frameCount) {
    if (file_ == nullptr) {
        return false;
    }
    const size_t bytes = frameCount * static_cast<size_t>(blockAlign_);
    if (bytes > 0) {
        const size_t written =
            std::fwrite(interleaved, 1, bytes, static_cast<std::FILE*>(file_));
        if (written != bytes) {
            core::Logger::instance().error("failed writing WAV data (disk full?)");
            return false;
        }
    }
    framesWritten_ += frameCount;
    return true;
}

bool WaveWriter::finalizeHeader() {
    std::FILE* file = static_cast<std::FILE*>(file_);
    const uint32_t dataBytes =
        static_cast<uint32_t>(framesWritten_ * static_cast<uint64_t>(blockAlign_));
    const uint32_t riffSize = 36 + dataBytes;

    if (std::fseek(file, 4, SEEK_SET) != 0 ||
        std::fwrite(&riffSize, 4, 1, file) != 1) {
        return false;
    }
    if (std::fseek(file, kRiffHeaderSize - 4, SEEK_SET) != 0 ||
        std::fwrite(&dataBytes, 4, 1, file) != 1) {
        return false;
    }
    if (std::fflush(file) != 0) {
        return false;
    }
    return true;
}

bool WaveWriter::close() {
    if (file_ == nullptr) {
        return true;
    }
    const bool ok = finalizeHeader();
    std::fclose(static_cast<std::FILE*>(file_));
    file_ = nullptr;
    return ok;
}

// ---------------------------------------------------------------------------
// Meter rendering
// ---------------------------------------------------------------------------

std::string renderMeter(double rmsDb, double peakDb, int width) {
    if (width < 4) {
        width = 4;
    }

    // Map dBFS in the range [-60, 0] to the bar.
    const double clampedRms = std::clamp(rmsDb, -60.0, 0.0);
    const double clampedPeak = std::clamp(peakDb, -60.0, 0.0);
    const int fillRms = static_cast<int>((clampedRms + 60.0) / 60.0 *
                                         static_cast<double>(width - 2));
    const int fillPeak = static_cast<int>((clampedPeak + 60.0) / 60.0 *
                                          static_cast<double>(width - 2));

    std::string bar(width, '-');
    bar.front() = '[';
    bar.back() = ']';
    for (int i = 0; i < fillRms && i + 1 < width - 1; ++i) {
        bar[static_cast<size_t>(i) + 1] = '=';
    }
    if (fillPeak > fillRms) {
        const int pos = std::min(fillPeak, width - 2);
        if (pos >= 0) {
            bar[static_cast<size_t>(pos) + 1] = 'o';
        }
    }

    char suffix[64] = {0};
    std::snprintf(suffix, sizeof(suffix), " RMS %5.1f dBFS  PEAK %5.1f dBFS",
                  rmsDb, peakDb);
    return bar + suffix;
}

}  // namespace audio
