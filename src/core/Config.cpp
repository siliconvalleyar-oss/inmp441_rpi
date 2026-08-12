#include "core/Config.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace core {

namespace {

bool parseUint32(const char* text, uint32_t* out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    unsigned long value = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parseDouble(const char* text, double* out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    double value = std::strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

bool isSupportedRate(uint32_t rate) {
    switch (rate) {
        case 8000:
        case 16000:
        case 24000:
        case 32000:
        case 44100:
        case 48000:
            return true;
        default:
            return false;
    }
}

}  // namespace

Config parseArgs(int argc, char* argv[]) {
    Config config;

    const std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "-h" || arg == "--help") {
            config.showHelp = true;
            config.valid = true;
            return config;
        }
        if (arg == "--version") {
            config.showVersion = true;
            config.valid = true;
            return config;
        }
        if (arg == "--level") {
            config.mode = RunMode::kLevelMeter;
        } else if (arg == "--wav") {
            config.mode = RunMode::kRecordWav;
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                config.outputFile = args[++i];
            }
        } else if (arg == "--dump") {
            config.mode = RunMode::kDumpRawWords;
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                uint32_t count = 0;
                if (parseUint32(args[i + 1].c_str(), &count)) {
                    config.dumpWordCount = count;
                    ++i;
                }
            }
        } else if (arg == "--info") {
            config.mode = RunMode::kInfo;
        } else if (arg == "--duration" || arg == "-d") {
            if (i + 1 >= args.size() || !parseDouble(args[i + 1].c_str(), &config.durationSeconds)) {
                config.valid = false;
                config.error = "--duration requires a numeric value (seconds)";
                return config;
            }
            ++i;
        } else if (arg == "--rate" || arg == "-r") {
            if (i + 1 >= args.size() || !parseUint32(args[i + 1].c_str(), &config.sampleRate)) {
                config.valid = false;
                config.error = "--rate requires a numeric value (Hz)";
                return config;
            }
            ++i;
        } else if (arg == "--channel" || arg == "-c") {
            if (i + 1 >= args.size()) {
                config.valid = false;
                config.error = "--channel requires 'left' or 'right'";
                return config;
            }
            const std::string& value = args[++i];
            if (value == "left") {
                config.selectLeftChannel = true;
            } else if (value == "right") {
                config.selectLeftChannel = false;
            } else {
                config.valid = false;
                config.error = "--channel must be 'left' or 'right'";
                return config;
            }
        } else if (arg == "--stereo") {
            config.recordStereo = true;
        } else if (arg == "--no-lr-gpio") {
            config.driveLrSelectGpio = false;
        } else if (arg == "--verbose" || arg == "-v") {
            config.verbose = true;
        } else {
            config.valid = false;
            config.error = "unknown option: " + arg;
            return config;
        }
    }

    if (!isSupportedRate(config.sampleRate)) {
        config.valid = false;
        config.error = "unsupported sample rate " + std::to_string(config.sampleRate) +
                       " (supported: 8000, 16000, 24000, 32000, 44100, 48000)";
        return config;
    }

    if (config.durationSeconds <= 0.0) {
        config.valid = false;
        config.error = "--duration must be greater than zero";
        return config;
    }

    return config;
}

void printUsage() {
    std::printf(
        "inmp441_rpi - I2S MEMS microphone (INMP441) reader for Raspberry Pi\n"
        "using the bcm2835 userspace library (raw PCM/I2S peripheral access).\n"
        "\n"
        "Usage: inmp441_rpi [options]\n"
        "\n"
        "Modes (default: level meter):\n"
        "  --level                 Live RMS/peak meter (default)\n"
        "  --wav [file.wav]        Record audio to a 16-bit PCM WAV file\n"
        "                          (default: output/recording.wav)\n"
        "  --dump [count]          Dump N raw 32-bit I2S words and exit (debug)\n"
        "  --info                  Print hardware/configuration info and exit\n"
        "\n"
        "Options:\n"
        "  -r, --rate <hz>         Sample rate (default 48000)\n"
        "  -d, --duration <sec>    Recording duration (default 5)\n"
        "  -c, --channel <lr>      INMP441 channel: 'left' or 'right' (default left)\n"
        "  --stereo                Record both I2S channels (right slot is silent\n"
        "                          with a single microphone)\n"
        "  --no-lr-gpio            Do not drive GPIO21 (leave L/R pin as wired)\n"
        "  -v, --verbose           Verbose (debug) logging\n"
        "  --version               Show version and exit\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Notes:\n"
        "  * Must run as root (the bcm2835 library needs /dev/mem access).\n"
        "  * All log output goes to stderr; stdout stays clean for piping.\n");
}

}  // namespace core
