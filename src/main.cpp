#include <chrono>
#include <cstdio>
#include <vector>

#include "audio/AudioProcessor.hpp"
#include "audio/INMP441.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/SignalHandler.hpp"

namespace {

using core::Config;
using core::LogLevel;
using core::Logger;
using core::RunMode;

// Frames fetched per I2S read burst (~5 ms at 48 kHz).
constexpr size_t kChunkFrames = 240;

void runInfoMode(const Config& config) {
    std::printf("inmp441_rpi -- hardware / configuration\n");
    std::printf("  board          : %s\n", audio::I2SController::boardInfo());
    std::printf("  sample rate    : %u Hz\n", config.sampleRate);
    std::printf("  BCLK           : %.3f MHz\n",
                static_cast<double>(config.sampleRate) * 64.0 / 1000000.0);
    std::printf("  I2S pins (GPIO): SCK=%d (18), WS=%d (19), SD=%d (20)\n", 18, 19, 20);
    std::printf("  L/R select     : GPIO 21 -> %s (physical pin 40)\n",
                config.driveLrSelectGpio ? (config.selectLeftChannel ? "LOW (left)" : "HIGH (right)")
                                          : "left as wired (--no-lr-gpio)");
    std::printf("  run as root     : yes (required by bcm2835)\n");
}

int runLevelMeter(audio::INMP441& mic, const Config& config) {
    Logger& log = Logger::instance();
    audio::RmsAnalyzer analyzer;
    std::vector<audio::AudioFrame> frames(kChunkFrames);

    const auto interval = std::chrono::duration<double, std::milli>(config.meterIntervalMs);
    auto nextRefresh = std::chrono::steady_clock::now() + interval;

    log.info("level meter started (Ctrl+C to stop)");

    while (!core::SignalHandler::shouldStop()) {
        const size_t read = mic.readFrames(frames.data(), frames.size());
        if (read == 0) {
            continue;
        }
        analyzer.addFrames(frames.data(), read, true);

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextRefresh) {
            std::string meter = audio::renderMeter(analyzer.rmsDb(), analyzer.peakDb(), 36);
            std::fprintf(stderr, "\r%s", meter.c_str());
            std::fflush(stderr);
            analyzer.reset();
            nextRefresh = now + interval;
        }
    }
    std::fprintf(stderr, "\n");
    return 0;
}

int runRecordMode(audio::INMP441& mic, const Config& config) {
    Logger& log = Logger::instance();

    audio::WaveWriter writer(config.outputFile, config.sampleRate,
                             config.recordStereo);
    if (!writer.open()) {
        return 1;
    }

    std::vector<audio::AudioFrame> frames(kChunkFrames);
    std::vector<int16_t> interleaved(kChunkFrames * (config.recordStereo ? 2 : 1));

    const auto start = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration<double>(config.durationSeconds);

    log.info("recording %u frames (%u Hz, %s) to '%s'",
             static_cast<uint32_t>(config.sampleRate * config.durationSeconds),
             config.sampleRate,
             config.recordStereo ? "stereo 16-bit" : "mono 16-bit (left)",
             config.outputFile.c_str());

    bool failed = false;
    while (!core::SignalHandler::shouldStop()) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= duration) {
            break;
        }

        const size_t read = mic.readFrames(frames.data(), frames.size());
        if (read == 0) {
            continue;
        }

        for (size_t i = 0; i < read; ++i) {
            if (config.recordStereo) {
                interleaved[i * 2] = frames[i].left16();
                interleaved[i * 2 + 1] = frames[i].right16();
            } else {
                interleaved[i] = frames[i].left16();
            }
        }

        if (!writer.writeFrames16(interleaved.data(), read)) {
            failed = true;
            break;
        }
    }

    if (!writer.close()) {
        failed = true;
    }

    if (failed) {
        log.error("recording failed");
        return 1;
    }

    const double seconds = config.durationSeconds;
    log.info("recorded %.1f s, %llu frames (%u bytes) -> %s",
             seconds,
             static_cast<unsigned long long>(writer.framesWritten()),
             static_cast<unsigned int>(writer.framesWritten() * (config.recordStereo ? 4U : 2U)),
             config.outputFile.c_str());
    return 0;
}

int runDumpMode(audio::INMP441& mic, const Config& config) {
    std::vector<uint32_t> words(config.dumpWordCount);
    const size_t read = mic.readRawWords(words.data(), words.size());

    std::printf("# raw I2S slots (32-bit), pairs = one stereo frame\n");
    std::printf("# alignment: 24-bit sample = slot >> 8 (arithmetic)\n");
    std::printf("# count: %zu of %u requested\n", read, config.dumpWordCount);
    for (size_t i = 0; i + 1 < read; i += 2) {
        std::printf("frame %04zu  L 0x%08X  R 0x%08X\n", i / 2, words[i], words[i + 1]);
    }
    if ((read & 1U) != 0U) {
        std::printf("frame %04zu  L 0x%08X\n", read / 2, words[read - 1]);
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    Config config = core::parseArgs(argc, argv);

    Logger& log = Logger::instance();
    log.setLevel(config.verbose ? LogLevel::kDebug : LogLevel::kInfo);

    if (!config.valid) {
        log.error("%s", config.error.c_str());
        core::printUsage();
        return 1;
    }

    if (config.showHelp) {
        core::printUsage();
        return 0;
    }
    if (config.showVersion) {
        std::printf("inmp441_rpi 1.0.0\n");
        return 0;
    }

    core::SignalHandler::install();

    audio::INMP441 mic;
    if (!mic.init(config.sampleRate, config.selectLeftChannel, config.driveLrSelectGpio)) {
        return 1;
    }

    int result = 0;
    switch (config.mode) {
        case RunMode::kInfo:
            runInfoMode(config);
            break;
        case RunMode::kLevelMeter:
            result = runLevelMeter(mic, config);
            break;
        case RunMode::kRecordWav:
            result = runRecordMode(mic, config);
            break;
        case RunMode::kDumpRawWords:
            result = runDumpMode(mic, config);
            break;
    }

    mic.close();
    return result;
}
