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

// Parses the command line. On failure `valid` is set to false and `error`
// contains a human-readable description.
Config parseArgs(int argc, char* argv[]);

void printUsage();

}  // namespace core
