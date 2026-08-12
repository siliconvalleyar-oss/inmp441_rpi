#pragma once

#include <cstdint>
#include <string>

namespace core {

// Application execution mode selected from the command line.
enum class RunMode {
    kLevelMeter,   // live RMS / peak meter (default)
    kRecordWav,    // record N seconds to a WAV file
    kRecordMp3,    // record N seconds to a WAV then encode to MP3 (lame)
    kDumpRawWords, // print N raw 32-bit I2S words (debug / alignment check)
    kInfo,         // print hardware / configuration info and exit
};

struct Config {
    RunMode mode = RunMode::kLevelMeter;
    std::string outputFile = "output/recording.wav";
    double durationSeconds = 5.0;
    uint32_t sampleRate = 48000;
    bool selectLeftChannel = true;   // L/R select: true = left, false = right
    bool driveLrSelectGpio = true;   // drive GPIO21 to control the L/R pin
    bool recordStereo = false;       // WAV: interleaved stereo instead of mono
    bool verbose = false;
    uint32_t dumpWordCount = 16;     // words printed by --dump
    double meterIntervalMs = 120.0;  // refresh period of the level meter
    bool showHelp = false;           // --help / -h requested (exit before init)
    bool showVersion = false;        // --version requested (exit before init)
    bool valid = true;
    std::string error;
};

// Parses the command line. On failure `valid` is set to false and `error`
// contains a human-readable description.
Config parseArgs(int argc, char* argv[]);

void printUsage();

}  // namespace core
