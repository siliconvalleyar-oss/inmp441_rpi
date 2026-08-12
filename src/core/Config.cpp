#include "core/Config.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Logger.hpp"

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

bool loadConfig(Config& config, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        core::Logger::instance().debug("no config file at '%s' (using defaults)",
                                       path.c_str());
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        config.sampleRate = j.value("sample_rate", config.sampleRate);
        config.selectLeftChannel = j.value("left_channel", config.selectLeftChannel);
        config.recordStereo = j.value("stereo", config.recordStereo);
        config.durationSeconds = j.value("duration_seconds", config.durationSeconds);
        config.warmupSeconds = j.value("warmup_seconds", config.warmupSeconds);
        config.gainDb = j.value("gain_db", config.gainDb);
        config.dropoutThresholdSeconds = j.value("dropout_seconds", config.dropoutThresholdSeconds);
        config.meterIntervalMs = j.value("meter_interval_ms", config.meterIntervalMs);
        config.recordMp3 = (j.value("format", std::string("wav")) == "mp3");
        config.btMac = j.value("bt_mac", config.btMac);
    } catch (const nlohmann::json::exception& e) {
        core::Logger::instance().warning(
            "config file '%s' is invalid, using defaults (%s)",
            path.c_str(), e.what());
        return false;
    }

    core::Logger::instance().info("loaded configuration from %s", path.c_str());
    return true;
}

bool saveConfig(const Config& config, const std::string& path) {
    nlohmann::json j;
    j["sample_rate"] = config.sampleRate;
    j["left_channel"] = config.selectLeftChannel;
    j["stereo"] = config.recordStereo;
    j["duration_seconds"] = config.durationSeconds;
    j["warmup_seconds"] = config.warmupSeconds;
    j["gain_db"] = config.gainDb;
    j["dropout_seconds"] = config.dropoutThresholdSeconds;
    j["meter_interval_ms"] = config.meterIntervalMs;
    j["format"] = config.recordMp3 ? "mp3" : "wav";
    j["bt_mac"] = config.btMac;

    std::ofstream file(path);
    if (!file.is_open()) {
        core::Logger::instance().error("cannot write configuration to '%s'",
                                       path.c_str());
        return false;
    }
    file << j.dump(2) << "\n";
    return true;
}

Config parseArgs(int argc, char* argv[], const Config& base) {
    Config config = base;

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
            config.recordMp3 = false;
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                config.outputFile = args[++i];
            } else {
                config.outputFile = defaultOutputName("wav");
            }
        } else if (arg == "--mp3") {
            config.mode = RunMode::kRecordMp3;
            config.recordMp3 = true;
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
        } else if (arg == "--player") {
            config.mode = RunMode::kPlayer;
        } else if (arg == "--bt-mac") {
            if (i + 1 >= args.size()) {
                config.valid = false;
                config.error = "--bt-mac requires a MAC address (XX:XX:XX:XX:XX:XX)";
                return config;
            }
            config.btMac = args[++i];
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
            if (config.gainDb < -kMaxGainDb || config.gainDb > kMaxGainDb) {
                core::Logger::instance().warning(
                    "--gain %.1f dB out of range, clamped to +-%.0f dB",
                    config.gainDb, kMaxGainDb);
                config.gainDb = clampGainDb(config.gainDb);
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
        } else if (arg == "--config") {
            if (i + 1 >= args.size()) {
                config.valid = false;
                config.error = "--config requires a file path";
                return config;
            }
            config.configFile = args[++i];
        } else if (arg == "--save-config") {
            config.saveConfigRequested = true;
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
        "                          (duration/channel/format, record, level test,\n"
        "                           play output/ files over Bluetooth)\n"
        "  --player                Play the WAV/MP3 files found in output/\n"
        "                          (Bluetooth A2DP via bluetoothctl + PulseAudio)\n"
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
        "      --config <file>     JSON config file to load/save (default\n"
        "                          config.json in the project directory)\n"
        "      --save-config       Persist the current settings to the config\n"
        "                          file (also done automatically when changing\n"
        "                          options in the interactive menu)\n"
        "      --bt-mac <mac>      Bluetooth A2DP speaker MAC, e.g.\n"
        "                          --bt-mac AA:BB:CC:DD:EE:FF. Saved as bt_mac\n"
        "                          in the config file; if empty, the first\n"
        "                          paired device is used automatically.\n"
        "  -v, --verbose           Verbose (debug) logging\n"
        "  --version               Show version and exit\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Persisted settings:\n"
        "  sample rate, channel, stereo, duration, warmup, gain, dropout\n"
        "  threshold, meter interval, menu format (WAV/MP3) and Bluetooth\n"
        "  MAC. CLI options always override the config file.\n"
        "\n"
        "Notes:\n"
        "  * Must run as root (the bcm2835 library needs /dev/mem access).\n"
        "  * All log output goes to stderr; stdout stays clean for piping.\n");
}

}  // namespace core
