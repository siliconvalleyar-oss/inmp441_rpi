#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
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

// Application version reported by --version and the menu banner.
constexpr const char* kAppVersion = "1.7.0";

// Level test duration used by the interactive menu.
constexpr double kMenuMeterSeconds = 5.0;

// Creates the parent directory (or directories) of an output path.
bool ensureParentDirectory(const std::string& outputPath) {
    const size_t pos = outputPath.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return true;
    }
    const std::string dir = outputPath.substr(0, pos);
    std::string current;
    size_t start = 0;
    while (true) {
        const size_t slash = dir.find('/', start);
        if (slash == std::string::npos) {
            break;
        }
        if (slash > start) {
            current = dir.substr(0, slash);
            if (::mkdir(current.c_str(), 0777) != 0 && errno != EEXIST) {
                return false;
            }
        }
        start = slash + 1;
    }
    if (dir.size() > 1 &&
        ::mkdir(dir.c_str(), 0777) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

void runInfoMode(const Config& config) {
    std::printf("inmp441_rpi -- hardware / configuration\n");
    std::printf("  board          : %s\n", audio::I2SController::boardInfo());
    std::printf("  sample rate    : %u Hz\n", config.sampleRate);
    std::printf("  BCLK           : %.3f MHz\n",
                static_cast<double>(config.sampleRate) * 64.0 / 1000000.0);
    std::printf("  I2S pins (GPIO): SCK=%d (18), WS=%d (19), SD=%d (20)\n", 18, 19, 20);
    std::printf("  mic channel     : %s (records the %s I2S slot)\n",
                config.selectLeftChannel ? "left" : "right",
                config.selectLeftChannel ? "left" : "right");
    if (config.driveLrSelectGpio) {
        std::printf("  L/R select     : GPIO 21 driven %s (physical pin 40)\n",
                    config.selectLeftChannel ? "LOW (left)" : "HIGH (right)");
    } else {
        std::printf("  L/R select     : not driven; wire the mic L/R pin to GND "
                    "(left) or +3V3 (right) yourself\n");
    }
    std::printf("  run as root     : yes (required by bcm2835)\n");
}

int runLevelMeter(audio::INMP441& mic, const Config& config) {    Logger& log = Logger::instance();
    audio::RmsAnalyzer analyzer;
    std::vector<audio::AudioFrame> frames(kChunkFrames);

    const auto interval = std::chrono::duration<double, std::milli>(config.meterIntervalMs);
    auto nextRefresh = std::chrono::steady_clock::now() + interval;

    const auto meterStart = std::chrono::steady_clock::now();
    const auto meterLimit = std::chrono::duration<double>(config.meterSeconds);

    log.info("level meter started%s",
             config.meterSeconds > 0.0 ? " (bounded)" : " (Ctrl+C to stop)");

    while (!core::SignalHandler::shouldStop()) {
        const size_t read = mic.readFrames(frames.data(), frames.size());
        if (read == 0) {
            continue;
        }
        analyzer.addFrames(frames.data(), read, config.selectLeftChannel);

        if (config.meterSeconds > 0.0 &&
            std::chrono::steady_clock::now() - meterStart >= meterLimit) {
            break;
        }

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

// Records `durationSeconds` of audio to a 16-bit WAV file at `path`.
bool recordWavToFile(audio::INMP441& mic, const Config& config, const std::string& path) {
    Logger& log = Logger::instance();

    audio::WaveWriter writer(path, config.sampleRate, config.recordStereo);
    if (!writer.open()) {
        return false;
    }

    std::vector<audio::AudioFrame> frames(kChunkFrames);
    std::vector<int16_t> interleaved(kChunkFrames * (config.recordStereo ? 2 : 1));

    auto start = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration<double>(config.durationSeconds);

    // Discard the I2S startup transient (the mic's internal HPF settles over
    // a few seconds) so recordings start clean.
    if (config.warmupSeconds > 0.0) {
        const auto warmupLimit = std::chrono::duration<double>(config.warmupSeconds);
        log.info("warming up %.1f s (discarding startup transient)...",
                 config.warmupSeconds);
        while (std::chrono::steady_clock::now() - start < warmupLimit &&
               !core::SignalHandler::shouldStop()) {
            if (mic.readFrames(frames.data(), frames.size()) == 0) {
                continue;
            }
        }
        log.info("warm-up done");
        start = std::chrono::steady_clock::now();
    }

    log.info("recording %u frames (%u Hz, %s) to '%s'",
             static_cast<uint32_t>(config.sampleRate * config.durationSeconds),
             config.sampleRate,
             config.recordStereo
                 ? "stereo 16-bit"
                 : (config.selectLeftChannel ? "mono 16-bit (left)" : "mono 16-bit (right)"),
             path.c_str());

    bool failed = false;

    // Digital gain (dB -> linear), applied when converting to 16-bit.
    const float gain =
        (config.gainDb == 0.0f) ? 1.0f : std::pow(10.0f, static_cast<float>(config.gainDb) / 20.0f);

    auto toInt16 = [gain](int16_t sample) -> int16_t {
        if (gain == 1.0f) {
            return sample;
        }
        long v = static_cast<long>(sample * gain);
        if (v > 32767L) {
            v = 32767L;
        } else if (v < -32768L) {
            v = -32768L;
        }
        return static_cast<int16_t>(v);
    };

    // Mic dropout detection: consecutive digital-silence (zero) samples.
    const size_t dropoutThreshold =
        static_cast<size_t>(config.sampleRate * config.dropoutThresholdSeconds);
    size_t zeroRun = 0;
    size_t dropoutEvents = 0;
    size_t dropoutFrames = 0;
    bool inDropout = false;

    // Optional live VU meter drawn to stderr during the recording.
    audio::RmsAnalyzer recordMeter;
    const auto meterInterval =
        std::chrono::duration<double, std::milli>(config.meterIntervalMs);
    auto nextMeter = std::chrono::steady_clock::now() + meterInterval;

    while (!core::SignalHandler::shouldStop()) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= duration) {
            break;
        }

        const size_t read = mic.readFrames(frames.data(), frames.size());
        if (read == 0) {
            continue;
        }

        if (config.showRecordMeter) {
            recordMeter.addFrames(frames.data(), read, config.selectLeftChannel);
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextMeter) {
                const std::string meter = audio::renderMeter(recordMeter.rmsDb(),
                                                             recordMeter.peakDb(), 36);
                std::fprintf(stderr, "\r%s  [%g s]", meter.c_str(), config.durationSeconds);
                std::fflush(stderr);
                recordMeter.reset();
                nextMeter = now + meterInterval;
            }
        }

        for (size_t i = 0; i < read; ++i) {
            const int16_t sample =
                config.recordStereo
                    ? (static_cast<int16_t>(frames[i].left16() | frames[i].right16()))
                    : (config.selectLeftChannel ? frames[i].left16() : frames[i].right16());

            if (sample == 0) {
                ++zeroRun;
                if (zeroRun >= dropoutThreshold && !inDropout) {
                    inDropout = true;
                    ++dropoutEvents;
                    log.warning("MIC DROPOUT: %g s of digital silence detected",
                                config.dropoutThresholdSeconds);
                }
                if (inDropout) {
                    ++dropoutFrames;
                }
            } else {
                if (inDropout) {
                    log.warning("mic recovered after %.2f s of silence",
                                static_cast<double>(zeroRun) / config.sampleRate);
                    inDropout = false;
                }
                zeroRun = 0;
            }

            if (config.recordStereo) {
                interleaved[i * 2] = toInt16(frames[i].left16());
                interleaved[i * 2 + 1] = toInt16(frames[i].right16());
            } else {
                interleaved[i] = toInt16(sample);
            }
        }

        if (!writer.writeFrames16(interleaved.data(), read)) {
            failed = true;
            break;
        }
    }

    if (config.showRecordMeter) {
        std::fprintf(stderr, "\n");
    }

    if (!writer.close()) {
        failed = true;
    }

    if (dropoutEvents > 0) {
        log.warning("AUDIO DROPOUTS: %zu event(s), %.2f s total digital silence in the recording",
                    dropoutEvents,
                    static_cast<double>(dropoutFrames) / config.sampleRate);
    } else {
        log.info("no mic dropouts detected (digital-silence threshold %.1f s)",
                 config.dropoutThresholdSeconds);
    }

    if (failed) {
        log.error("recording failed");
        return false;
    }

    log.info("recorded %.1f s, %llu frames (%u bytes) -> %s",
             config.durationSeconds,
             static_cast<unsigned long long>(writer.framesWritten()),
             static_cast<unsigned int>(writer.framesWritten() * (config.recordStereo ? 4U : 2U)),
             path.c_str());
    return true;
}

int runRecordMode(audio::INMP441& mic, const Config& config) {
    Logger& log = Logger::instance();

    if (!ensureParentDirectory(config.outputFile)) {
        log.error("cannot create output directory for '%s'", config.outputFile.c_str());
        return 1;
    }

    if (!recordWavToFile(mic, config, config.outputFile)) {
        return 1;
    }
    return 0;
}

int runRecordMp3Mode(audio::INMP441& mic, const Config& config) {
    Logger& log = Logger::instance();

    if (!ensureParentDirectory(config.outputFile)) {
        log.error("cannot create output directory for '%s'", config.outputFile.c_str());
        return 1;
    }

    // Record to a temporary WAV, then transcode with lame.
    const std::string tmpWav =
        "/tmp/inmp441_record_" + std::to_string(static_cast<long>(::getpid())) + ".wav";
    if (!recordWavToFile(mic, config, tmpWav)) {
        std::remove(tmpWav.c_str());
        return 1;
    }

    // Encode a maximally-compatible MP3: CBR 128 kbps mono @ 44.1 kHz
    // (WhatsApp and most instant messengers reject 48 kHz MP3s).
    const std::string cmd = "lame --silent --resample 44100 -b 128 " + tmpWav + " " +
                            config.outputFile;
    log.info("encoding MP3 (128 kbps CBR @ 44.1 kHz) with lame...");
    const int rc = std::system(cmd.c_str());
    std::remove(tmpWav.c_str());

    if (rc != 0) {
        log.error("lame failed (exit %d); is lame installed?", rc);
        return 1;
    }

    log.info("saved MP3 -> %s", config.outputFile.c_str());
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

// Interactive menu shown after a console presentation. Lets the operator
// configure the recording duration (min 5 s by default), channel, output
// format and file, then record or run a bounded level test.
int runMenuMode(audio::INMP441& mic, const Config& initial) {
    Logger& log = Logger::instance();
    Config config = initial;
    config.mode = RunMode::kRecordWav;

    while (true) {
        std::printf("\n");
        std::printf("============================================================\n");
        std::printf("  inmp441_rpi %s - INMP441 I2S microphone recorder\n", kAppVersion);
        std::printf("============================================================\n");
        std::printf("  Board   : %s\n", audio::I2SController::boardInfo());
        std::printf("  Pins    : SCK=GPIO18  WS=GPIO19  SD=GPIO20\n");
        std::printf("  Rate    : %u Hz\n", config.sampleRate);
        std::printf("  Channel : %s (L/R pin -> %s)\n",
                    config.selectLeftChannel ? "left" : "right",
                    config.selectLeftChannel ? "GND" : "+3V3");
        std::printf("  Format  : %s\n",
                    config.mode == RunMode::kRecordMp3 ? "MP3 (lame)" : "WAV");
        std::printf("  File    : %s\n", config.outputFile.c_str());
        std::printf("------------------------------------------------------------\n");
        std::printf("  1) Duration ....... %g s (min 5 s for test recordings)\n",
                    config.durationSeconds);
        std::printf("  2) Channel ........ %s\n",
                    config.selectLeftChannel ? "left" : "right");
        std::printf("  3) Format ......... %s\n",
                    config.mode == RunMode::kRecordMp3 ? "MP3" : "WAV");
        std::printf("  4) Level test ..... live meter for %g s\n", kMenuMeterSeconds);
        std::printf("  5) RECORD\n");
        std::printf("  0/Q) Quit\n");
        std::printf("------------------------------------------------------------\n");
        std::printf("Choice> ");
        std::fflush(stdout);

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::printf("\nBye.\n");
            break;
        }
        if (line.empty()) {
            continue;
        }
        const char choice =
            static_cast<char>(std::tolower(static_cast<unsigned char>(line[0])));

        switch (choice) {
            case '1': {
                std::printf("Duration in seconds (min 1) [%g]> ", config.durationSeconds);
                std::fflush(stdout);
                std::string value;
                std::getline(std::cin, value);
                if (!value.empty()) {
                    char* end = nullptr;
                    const double d = std::strtod(value.c_str(), &end);
                    if (end != value.c_str() && *end == '\0' && d >= 1.0) {
                        config.durationSeconds = d;
                    } else {
                        std::printf("  (ignored: must be a number >= 1)\n");
                    }
                }
                break;
            }
            case '2':
                config.selectLeftChannel = !config.selectLeftChannel;
                mic.setChannel(config.selectLeftChannel, config.driveLrSelectGpio);
                log.info("channel set to %s (L/R pin -> %s)",
                         config.selectLeftChannel ? "left" : "right",
                         config.selectLeftChannel ? "GND" : "+3V3");
                break;
            case '3':
                if (config.mode == RunMode::kRecordMp3) {
                    config.mode = RunMode::kRecordWav;
                    config.outputFile = "output/recording.wav";
                } else {
                    config.mode = RunMode::kRecordMp3;
                    config.outputFile = "output/recording.mp3";
                }
                break;
            case '4': {
                Config meterConfig = config;
                meterConfig.meterSeconds = kMenuMeterSeconds;
                runLevelMeter(mic, meterConfig);
                break;
            }
            case '5': {
                // Show the live VU meter while recording from the menu.
                config.showRecordMeter = true;
                const int rc = (config.mode == RunMode::kRecordMp3)
                                   ? runRecordMp3Mode(mic, config)
                                   : runRecordMode(mic, config);
                if (rc == 0) {
                    std::printf("\nRecorded OK. Press Enter to return to the menu...");
                    std::fflush(stdout);
                    std::getline(std::cin, line);
                    std::printf("\n");
                }
                break;
            }
            case '0':
            case 'q':
                std::printf("Bye.\n");
                return 0;
            default:
                std::printf("  (invalid choice; try 1-5, 0/Q)\n");
                break;
        }
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
        std::printf("inmp441_rpi %s\n", kAppVersion);
        return 0;
    }

    core::SignalHandler::install();

    audio::INMP441 mic;
    if (!mic.init(config.sampleRate, config.selectLeftChannel, config.driveLrSelectGpio)) {
        return 1;
    }

    int result = 0;
    switch (config.mode) {
        case RunMode::kMenu:
            result = runMenuMode(mic, config);
            break;
        case RunMode::kInfo:
            runInfoMode(config);
            break;
        case RunMode::kLevelMeter:
            result = runLevelMeter(mic, config);
            break;
        case RunMode::kRecordWav:
            result = runRecordMode(mic, config);
            break;
        case RunMode::kRecordMp3:
            result = runRecordMp3Mode(mic, config);
            break;
        case RunMode::kDumpRawWords:
            result = runDumpMode(mic, config);
            break;
    }

    mic.close();
    return result;
}
