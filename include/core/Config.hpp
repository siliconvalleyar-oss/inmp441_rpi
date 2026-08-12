#pragma once

#include <cstdint>
#include <string>

namespace core {

// Application execution mode selected from the command line.
enum class RunMode {
    kMenu,         // interactive menu after a console presentation (default)
    kLevelMeter,   // live RMS / peak meter
    kRecordWav,    // record N seconds to a WAV file
    kRecordMp3,    // record N seconds to a WAV then encode to MP3 (lame)
    kDumpRawWords, // print N raw 32-bit I2S words (debug / alignment check)
    kPlayer,       // play the WAV/MP3 files in output/ (menu or --player)
    kInfo,         // print hardware / configuration info and exit
};

struct Config {
    RunMode mode = RunMode::kMenu;
    // Output file. Empty until set by parseArgs; when left empty in a record
    // mode the code picks a timestamped default (recording_YYYYMMDDHHMM.<ext>).
    std::string outputFile;
    double durationSeconds = 5.0;
    uint32_t sampleRate = 48000;
    bool selectLeftChannel = true;   // L/R select: true = left, false = right
    bool driveLrSelectGpio = true;   // drive GPIO21 to control the L/R pin
    bool recordStereo = false;       // WAV: interleaved stereo instead of mono
    bool showRecordMeter = false;    // live VU meter on stderr during recording
    double warmupSeconds = 4.0;      // discard startup transient before recording
    double gainDb = 0.0;             // digital gain applied on write (dB)
    double dropoutThresholdSeconds = 1.0;  // min digital-silence run to flag
    bool verbose = false;
    uint32_t dumpWordCount = 16;     // words printed by --dump
    double meterIntervalMs = 120.0;  // refresh period of the level meter
    double meterSeconds = 0.0;       // level meter duration; 0 = until Ctrl+C
    // Persisted settings (JSON, see loadConfig/saveConfig).
    std::string configFile = "config.json";  // path used by --config / menu auto-save
    bool saveConfigRequested = false;         // --save-config: persist the CLI config
    bool recordMp3 = false;                   // menu format preference ("format" in JSON)
    std::string btMac;                        // Bluetooth A2DP speaker MAC ("bt_mac" in JSON)
    bool showHelp = false;           // --help / -h requested (exit before init)
    bool showVersion = false;        // --version requested (exit before init)
    bool valid = true;
    std::string error;
};

// Builds the default output file name with a local-time stamp down to the
// minute, e.g. "output/recording_202608121137.wav". Used when no explicit
// file name is given on the command line (--wav/--mp3 without a file).
std::string defaultOutputName(const char* extension);

// True when `mode` records audio to a file (WAV or MP3).
constexpr bool isRecordMode(RunMode mode) {
    return mode == RunMode::kRecordWav || mode == RunMode::kRecordMp3;
}

// Sane range for the digital gain in dB (clamped at parse time).
constexpr double kMaxGainDb = 60.0;

inline double clampGainDb(double gainDb) {
    return gainDb < -kMaxGainDb ? -kMaxGainDb : (gainDb > kMaxGainDb ? kMaxGainDb : gainDb);
}

// Loads persisted settings from `path` into `config` (only the fields stored
// by saveConfig). Returns false when the file is missing or invalid; in that
// case the caller keeps its current values. Logs a message either way.
bool loadConfig(Config& config, const std::string& path);

// Writes the persisted settings of `config` to `path` as pretty-printed JSON.
// Returns false on I/O errors.
bool saveConfig(const Config& config, const std::string& path);

// Parses the command line on top of `base` (defaults, already overlaid with
// any persisted JSON settings). On failure `valid` is set to false and
// `error` contains a human-readable description.
Config parseArgs(int argc, char* argv[], const Config& base = Config());

void printUsage();

}  // namespace core
