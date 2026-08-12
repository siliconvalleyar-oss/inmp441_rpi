#include "core/Config.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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

std::string defaultOutputName(const char* extension) {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char stamp[16] = {0};
    std::strftime(stamp, sizeof(stamp), "%Y%m%d%H%M", &tm);
    return std::string("output/recording_") + stamp + "." + extension;
}

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
        if (arg == "--menu") {
            config.mode = RunMode::kMenu;
        } else if (arg == "--level") {
            config.mode = RunMode::kLevelMeter;
        } else if (arg == "--wav") {
            config.mode = RunMode::kRecordWav;
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                config.outputFile = args[++i];
            } else {
                config.outputFile = defaultOutputName("wav");
            }
        } else if (arg == "--mp3") {
            config.mode = RunMode::kRecordMp3;
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                config.outputFile = args[++i];
            } else {
                config.outputFile = defaultOutputName("mp3");
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
        } else if (arg == "--dropout") {
            if (i + 1 >= args.size() ||
                !parseDouble(args[i + 1].c_str(), &config.dropoutThresholdSeconds)) {
                config.valid = false;
                config.error = "--dropout requires a numeric value (seconds)";
                return config;
            }
            ++i;
        } else if (arg == "--gain") {
            if (i + 1 >= args.size() || !parseDouble(args[i + 1].c_str(), &config.gainDb)) {
                config.valid = false;
                config.error = "--gain requires a numeric value (dB)";
                return config;
            }
            ++i;
        } else if (arg == "--warmup") {
            if (i + 1 >= args.size() || !parseDouble(args[i + 1].c_str(), &config.warmupSeconds)) {
                config.valid = false;
                config.error = "--warmup requires a numeric value (seconds)";
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
        } else if (arg == "--meter") {
            config.showRecordMeter = true;
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

    // Record modes without an explicit file name get a timestamped default
    // (e.g. output/recording_202608121137.wav).
    if (isRecordMode(config.mode) && config.outputFile.empty()) {
        config.outputFile = defaultOutputName(config.mode == RunMode::kRecordMp3 ? "mp3" : "wav");
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
        "Modes (default: interactive menu):\n"
        "  --menu                  Interactive menu after a console presentation\n"
        "                          (duration/channel/format, record, level test)\n"
        "  --level                 Live RMS/peak meter\n"
        "  --wav [file.wav]        Record audio to a 16-bit PCM WAV file\n"
        "                          (default: output/recording_YYYYMMDDHHMM.wav)\n"
        "  --mp3 [file.mp3]        Record to a temp WAV then encode to MP3\n"
        "                          with lame (default: output/recording_YYYYMMDDHHMM.mp3)\n"
        "  --dump [count]          Dump N raw 32-bit I2S words and exit (debug)\n"
        "  --info                  Print hardware/configuration info and exit\n"
        "\n"
        "Options:\n"
        "  -r, --rate <hz>         Sample rate (default 48000)\n"
        "  -d, --duration <sec>    Recording duration (default 5, min 1)\n"
        "      --warmup <sec>       Seconds of audio discarded before recording\n"
        "                          (default 4; removes the I2S startup transient;\n"
        "                           set 0 to disable)\n"
        "      --gain <db>          Digital gain applied to recordings (default 0).\n"
        "                          The INMP441 is very quiet for speech: try +20\n"
        "                          to +30 dB for close talk, +40 dB for room\n"
        "                          ambience. Clips at full scale.\n"
        "      --dropout <sec>      Flag runs of digital silence longer than this\n"
        "                          (default 1 s) as mic dropouts in the recording\n"
        "                          summary (diagnosis of flaky wiring/capsule)\n"
        "  -c, --channel <lr>      Mic channel and I2S slot to read:\n"
        "                          'left' (L/R pin -> GND) or 'right' (L/R pin\n"
        "                          -> +3V3). Also drives GPIO21 to match.\n"
        "  --stereo                Record both I2S channels (the unused slot is\n"
        "                          silent with a single microphone)\n"
        "  --meter                 Show a live VU meter on stderr while recording\n"
        "  --no-lr-gpio            Do not drive GPIO21; the mic L/R pin is wired\n"
        "                          to GND (left) or +3V3 (right) permanently\n"
        "  -v, --verbose           Verbose (debug) logging\n"
        "  --version               Show version and exit\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Notes:\n"
        "  * Must run as root (the bcm2835 library needs /dev/mem access).\n"
        "  * All log output goes to stderr; stdout stays clean for piping.\n");
}

}  // namespace core
