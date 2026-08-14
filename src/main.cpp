#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "audio/AudioProcessor.hpp"
#include "audio/INMP441.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "core/SignalHandler.hpp"
#include "oled/oled_display.hpp"
#include "sound/player.hpp"
#include "sound/track_list.hpp"
#include "tools/bluetooth_tool.hpp"

namespace {

using core::Config;
using core::LogLevel;
using core::Logger;
using core::RunMode;
using INMP441::Inmp441_t;
using BLUETOOTH::BluetoothTool;
using OLED::OledDisplay;
using PLAYER::Player;
using SOUND_LIST::TrackList;

// Frames fetched per I2S read burst (~5 ms at 48 kHz).
constexpr size_t kChunkFrames = 240;

// Application version reported by --version and the menu banner.
constexpr const char* kAppVersion = "1.7.7";

// Version shown at runtime: read from the VERSION file at the working
// directory (the repo root when launched with make run), so the menu banner
// always reflects the tagged release. Falls back to the compiled-in constant
// when the file is missing or unreadable.
const char* appVersion() {
    static std::string version;
    if (version.empty()) {
        std::ifstream file("VERSION");
        std::string line;
        if (file && std::getline(file, line)) {
            const size_t first = line.find_first_not_of(" \t\r\n");
            const size_t last = line.find_last_not_of(" \t\r\n");
            if (first != std::string::npos && last >= first) {
                version = line.substr(first, last - first + 1);
            }
        }
    }
    return version.empty() ? kAppVersion : version.c_str();
}

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

int runLevelMeter(Inmp441_t& mic, const Config& config) {    Logger& log = Logger::instance();
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
bool recordWavToFile(Inmp441_t& mic, const Config& config, const std::string& path) {
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

    // Digital gain (dB -> linear). Applied in the 24-bit/float domain and the
    // final quantization to 16-bit happens once, at the very end, so the gain
    // does NOT amplify the 16-bit quantization noise of an early truncation.
    const double gain =
        (config.gainDb == 0.0) ? 1.0 : std::pow(10.0, config.gainDb / 20.0);

    // Adjustable one-pole high-pass filter (default 30 Hz, 0 = off). Removes
    // the INMP441's DC offset and the sub-bass hum of the power rail BEFORE
    // the digital gain: at high gain those constants would be amplified into
    // a huge excursion that saturates (clips) the recording - the harsh
    // "noise" heard at high gain settings. Unlike the old fixed DC blocker,
    // the filter also runs when no gain is applied (hpf_hz > 0).
    // Per-channel state: in stereo, L and R must not share one filter memory
    // (that would cross-couple the two slots).
    audio::HighPassFilter hpfLeft;
    audio::HighPassFilter hpfRight;
    hpfLeft.setCutoffHz(config.hpfHz, config.sampleRate);
    hpfRight.setCutoffHz(config.hpfHz, config.sampleRate);

    // Adjustable one-pole low-pass filter (default 0 Hz = off). Attenuates
    // high-frequency noise and the INMP441's ultrasonic response. Use values
    // like 8000 or 12000 Hz to reduce hiss. Runs AFTER the HPF so the signal
    // band is clean before the digital gain is applied.
    audio::LowPassFilter lpfLeft;
    audio::LowPassFilter lpfRight;
    lpfLeft.setCutoffHz(config.lpfHz, config.sampleRate);
    lpfRight.setCutoffHz(config.lpfHz, config.sampleRate);

    // Applies HPF, then LPF (when enabled), then the digital gain - all in the
    // 24-bit/float domain - and quantizes to 16-bit with rounding exactly once,
    // at the end. Preserves the 8 bits of resolution the 24-bit ADC adds over
    // 16-bit, so the gain does not amplify quantization noise. Clamped samples
    // are counted in `clipCount` (diagnosis aid when --gain is too high).
    auto applyFx = [gain](audio::HighPassFilter& hpf, audio::LowPassFilter& lpf,
                          int32_t sample24, size_t* clipCount) -> int16_t {
        if (hpf.enabled()) {
            sample24 = hpf.process(sample24);
        }
        if (lpf.enabled()) {
            sample24 = lpf.process(sample24);
        }
        // 24-bit sample -> float [-1, 1), apply gain, then quantize to 16-bit
        // with rounding. One quantization step for the whole chain.
        const double v = static_cast<double>(sample24) / 8388608.0 * gain;
        long out = static_cast<long>(std::lround(v * 32768.0));
        if (out > 32767L) {
            out = 32767L;
            if (clipCount != nullptr) {
                ++(*clipCount);
            }
        } else if (out < -32768L) {
            out = -32768L;
            if (clipCount != nullptr) {
                ++(*clipCount);
            }
        }
        return static_cast<int16_t>(out);
    };

    // Mic dropout detection: consecutive digital-silence (zero) samples.
    const size_t dropoutThreshold =
        static_cast<size_t>(config.sampleRate * config.dropoutThresholdSeconds);
    size_t zeroRun = 0;
    size_t dropoutEvents = 0;
    size_t dropoutFrames = 0;
    bool inDropout = false;

    // Output samples clamped to full scale (diagnosis aid: --gain too high).
    size_t clipCount = 0;

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
            const int32_t leftSample = frames[i].left24;
            const int32_t rightSample = frames[i].right24;
            const int32_t activeSample =
                config.selectLeftChannel ? leftSample : rightSample;

            // Dropout detection works on the raw 24-bit value: a real zero slot
            // is digital silence; quiet audio is still non-zero in 24-bit (it
            // only rounds to zero after the 16-bit truncation).
            if (activeSample == 0) {
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
                interleaved[i * 2] = applyFx(hpfLeft, lpfLeft, leftSample, &clipCount);
                interleaved[i * 2 + 1] = applyFx(hpfRight, lpfRight, rightSample, &clipCount);
            } else {
                interleaved[i] = applyFx(hpfLeft, lpfLeft, activeSample, &clipCount);
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

    if (clipCount > 0) {
        const double clipSeconds =
            static_cast<double>(clipCount) / config.sampleRate;
        log.warning("CLIPPING: %zu sample(s) (%.2f s) clamped to full scale - "
                    "lower --gain (try %+.1f dB) for clean audio",
                    clipCount, clipSeconds, core::clampGainDb(config.gainDb - 6.0));
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

int runRecordMode(Inmp441_t& mic, const Config& config) {
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

int runRecordMp3Mode(Inmp441_t& mic, const Config& config) {
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

int runDumpMode(Inmp441_t& mic, const Config& config) {
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

// ---------------------------------------------------------------------------
// Player UI: raw-key screen listing the WAV/MP3 files of output/, with OLED
// status and PulseAudio volume control. Loops until 'q' / Ctrl+C.
// ---------------------------------------------------------------------------

// Terminal state for the player screen (raw keys, no Enter needed).
struct termios g_savedTerm;
bool g_termRaw = false;

bool enableRawTerm() {
    if (tcgetattr(STDIN_FILENO, &g_savedTerm) != 0) {
        return false;
    }
    struct termios raw = g_savedTerm;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return false;
    }
    g_termRaw = true;
    return true;
}

void restoreRawTerm() {
    if (g_termRaw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
        g_termRaw = false;
    }
}

// Internal key codes for arrow keys.
constexpr int kKeyUp = -1;
constexpr int kKeyDown = -2;
constexpr int kKeyLeft = -3;
constexpr int kKeyRight = -4;

// Timeout of the key loop (ms): drives the OLED marquee refresh.
constexpr int kPlayerKeyTimeoutMs = 250;

// Reads one key; returns 0 when the timeout expires without input.
int readPlayerKey(int timeoutMs) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = static_cast<suseconds_t>((timeoutMs % 1000) * 1000);

    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0) {
        return 0;
    }

    unsigned char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1) {
        return 0;
    }

    if (c == 0x1B) {  // escape sequence (arrow keys)
        char seq[2];
        if (::read(STDIN_FILENO, &seq[0], 1) != 1) return 0x1B;
        if (::read(STDIN_FILENO, &seq[1], 1) != 1) return 0x1B;
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return kKeyUp;
                case 'B': return kKeyDown;
                case 'C': return kKeyRight;
                case 'D': return kKeyLeft;
                default: break;
            }
        }
        return 0x1B;
    }
    return static_cast<int>(c);
}

// Runs a command and returns its standard output.
std::string runCapture(const std::string& cmd) {
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return out;
    }
    char buf[128];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out += buf;
    }
    pclose(pipe);
    return out;
}

// Volume of the default PulseAudio sink, or -1 if unavailable. PulseAudio
// permits volumes above 100% (up to ~150% when the user boosts the sink), so
// values up to 150 are shown instead of hiding the readout.
int readPulseVolume() {
    const std::string out = runCapture(
        "pactl list sinks 2>/dev/null | awk -F'/' '/Volume:/"
        "{gsub(/ /,\"\",$2); print $2; exit}'");
    const int v = std::atoi(out.c_str());
    return (v >= 0 && v <= 150) ? v : -1;
}

// Player screen: lists the tracks of output/ and controls playback with
// raw keys. Loops until 'q' or Ctrl+C; restores the terminal on exit.
int runPlayerScreen(TrackList& tracks, Player& player, OledDisplay& oled,
                    const std::string& deviceMac) {
    Logger& log = Logger::instance();
    if (tracks.empty()) {
        log.error("no playable files in '%s'", tracks.directory().c_str());
        return 1;
    }

    if (!enableRawTerm()) {
        log.warning("terminal is not interactive; keys need Enter "
                    "(use 'ssh -t' for the player screen)");
    }

    std::size_t cursor = 0;
    int scrollOffset = 0;
    int volume = readPulseVolume();
    bool quit = false;

    std::printf("\033[H\033[2J");
    std::printf("=== Playback v%s (output/) | Bluetooth: %s ===\n",
                appVersion(),
                deviceMac.empty() ? "(none)" : deviceMac.c_str());
    std::printf("  w/s or arrows: navigate   space/p: play/pause   "
                "+/-: volume   q: quit\n\n");
    std::fflush(stdout);

    while (!quit) {
        // Ctrl+C goes through core::SignalHandler; bail out if requested.
        if (core::SignalHandler::shouldStop()) {
            quit = true;
            break;
        }

        const PLAYER::State st = player.state();
        const bool playing = (st == PLAYER::State::Playing ||
                              st == PLAYER::State::Paused);
        const bool paused = (st == PLAYER::State::Paused);

        const double posSec = playing ? player.position() : 0.0;
        const double durSec = playing ? player.duration() : 0.0;

        oled.showTrack(static_cast<int>(cursor), static_cast<int>(tracks.size()),
                       tracks.nameAt(cursor), playing, paused, scrollOffset,
                       volume, appVersion(), posSec, durSec);

        // Redraw the console frame (cursor home + per-line clear).
        std::fputs("\033[H", stdout);
        std::printf("=== Playback v%s (output/) | Bluetooth: %s ===\033[K\n",
                    appVersion(),
                    deviceMac.empty() ? "(none)" : deviceMac.c_str());
        std::printf("State: %s", paused ? "PAUSED" : (playing ? "PLAYING" : "STOPPED"));
        if (volume >= 0) {
            std::printf(" | Vol: %d%%", volume);
        }
        std::printf(" | Track %zu/%zu\033[K\n", cursor + 1, tracks.size());

        // Barra de progreso (24 celdas ASCII) + tiempo transcurrido / total.
        {
            char bar[25];
            const int filled = (durSec > 0.0)
                ? static_cast<int>(posSec / durSec * 24.0 + 0.5) : 0;
            int f = (filled < 0) ? 0 : (filled > 24 ? 24 : filled);
            for (int i = 0; i < 24; ++i) bar[i] = (i < f) ? '#' : '-';
            bar[24] = '\0';

            const int pm = static_cast<int>(posSec) / 60;
            const int ps = static_cast<int>(posSec) % 60;
            if (durSec > 0.0) {
                const int tm = static_cast<int>(durSec) / 60;
                const int ts = static_cast<int>(durSec) % 60;
                std::printf(" [%s] %d:%02d / %d:%02d\033[K\n", bar, pm, ps, tm, ts);
            } else {
                std::printf(" [%s] %d:%02d / --:--\033[K\n", bar, pm, ps);
            }
        }

        const std::size_t start = (cursor >= 6) ? (cursor - 6) : 0;
        const std::size_t end = (start + 12 < tracks.size()) ? (start + 12) : tracks.size();
        for (std::size_t i = start; i < end; ++i) {
            if (i == cursor) {
                std::printf(" > %02zu. %s\033[K\n", i + 1, tracks.nameAt(i).c_str());
            } else {
                std::printf("   %02zu. %s\033[K\n", i + 1, tracks.nameAt(i).c_str());
            }
        }
        std::printf("\n w/s: nav  space/p: play-pause  +/-: vol  q: quit\033[K\n");
        std::fputs("\033[J", stdout);
        std::fflush(stdout);

        const int key = readPlayerKey(kPlayerKeyTimeoutMs);
        ++scrollOffset;  // advances the OLED marquee

        switch (key) {
            case kKeyUp:
            case kKeyLeft:
            case 'w':
            case 'a': {
                const bool wasPlaying = playing;
                if (wasPlaying) {
                    player.stop();
                }
                cursor = (cursor + tracks.size() - 1) % tracks.size();
                scrollOffset = 0;  // restart the marquee for the new track
                if (wasPlaying) {
                    player.play(tracks.pathAt(cursor));
                }
                break;
            }
            case kKeyDown:
            case kKeyRight:
            case 's':
            case 'd': {
                const bool wasPlaying = playing;
                if (wasPlaying) {
                    player.stop();
                }
                cursor = (cursor + 1) % tracks.size();
                scrollOffset = 0;
                if (wasPlaying) {
                    player.play(tracks.pathAt(cursor));
                }
                break;
            }
            case '\r':
            case ' ':
            case 'p':
                if (playing) {
                    player.togglePause();
                } else {
                    player.play(tracks.pathAt(cursor));
                }
                break;
            case '+':
            case '=':
                std::system("pactl set-sink-volume @DEFAULT_SINK@ +5% >/dev/null 2>&1");
                volume = readPulseVolume();
                break;
            case '-':
            case '_':
                std::system("pactl set-sink-volume @DEFAULT_SINK@ -5% >/dev/null 2>&1");
                volume = readPulseVolume();
                break;
            case 'q':
            case 'Q':
            case 0x03:  // Ctrl+C
                quit = true;
                break;
            default:
                break;
        }
    }

    player.stop();
    restoreRawTerm();
    std::printf("\nBye.\n");
    return 0;
}

// Playback mode: scan output/, connect the Bluetooth speaker (--bt-mac, the
// persisted bt_mac, or the first paired device) and run the player screen.
int runPlayerMode(const Config& config) {
    Logger& log = Logger::instance();

    // El OLED (SSD1306 por I2C) usa la librería bcm2835, que necesita root.
    // Se inicializa AQUÍ, ANTES de bajar privilegios para PulseAudio: un
    // bcm2835_init() fallido (ejecutado como uid no-root) deja el estado
    // global de la librería corrupto y bcm2835_close() del micrófono
    // segfaulta al salir del proceso.
    OledDisplay oled;
    const bool oledReady = oled.init();
    if (oledReady) {
        oled.showMessage("inmp441 player", config.btMac);
    }

    // make run/run.sh lanzan la app con sudo (necesario para /dev/mem al
    // grabar). PulseAudio es por-usuario y rechaza a root (Access denied):
    // dropear privilegios al usuario real para que pactl y libao vean el
    // sink Bluetooth, y restaurar root al salir de este modo.
    //
    // El desbloqueo rfkill del adaptador solo puede hacerlo root y ademas
    // debe ejecutarse antes de conectar (que ocurre ya con euid != 0).
    BluetoothTool::unblockRfkill();
    const bool droppedPrivs = BluetoothTool::dropToPulseUser();
    struct RestorePrivsGuard {
        ~RestorePrivsGuard() {
            if (dropped) {
                BluetoothTool::restorePulseUser();
            }
        }
        bool dropped = false;
    } restoreGuard;
    restoreGuard.dropped = droppedPrivs;

    TrackList tracks(TrackList::kDefaultDir);
    if (!tracks.load()) {
        return 1;
    }
    if (tracks.empty()) {
        log.error("no WAV/MP3 files in '%s' - record something first "
                  "(menu option 6 or --wav/--mp3)", tracks.directory().c_str());
        return 1;
    }

    // Política estricta de un solo dispositivo: el altavoz debe estar
    // configurado con --bt-mac (o bt_mac en el config) y conectado. No hay
    // auto-detección ni caída al altavoz local: el audio solo sale por él.
    BluetoothTool bt;
    const std::string mac = config.btMac;
    if (mac.empty()) {
        log.error("no Bluetooth speaker configured: set --bt-mac or bt_mac "
                  "in the config file (required for --player)");
        return 1;
    }
    if (!bt.connect(mac)) {
        log.error("Bluetooth speaker %s is not available; playback aborted "
                  "(audio only plays through that device)", mac.c_str());
        return 1;
    }

    Player player;

    // The OledDisplay destructor powers the screen down; the microphone
    // owns the bcm2835 mapping, so nothing is closed here.
    return runPlayerScreen(tracks, player, oled, mac);
}

// Interactive menu shown after a console presentation. Lets the operator
// configure the recording duration, channel, gain, output format and file,
// then record or run a bounded level test. Every change is persisted to the
// JSON config file automatically.
int runMenuMode(Inmp441_t& mic, const Config& initial) {
    Logger& log = Logger::instance();
    Config config = initial;
    config.mode = config.recordMp3 ? RunMode::kRecordMp3 : RunMode::kRecordWav;

    // The interactive menu has no file option: always record to a fresh
    // timestamped name (recording_YYYYMMDDHHMM.<ext>), respecting the
    // persisted format preference ("format" in config.json).
    if (config.outputFile.empty()) {
        config.outputFile =
            core::defaultOutputName(config.recordMp3 ? "mp3" : "wav");
    }

    // Persists the current settings; called after every menu change.
    auto persistConfig = [&config, &log]() {
        if (core::saveConfig(config, config.configFile)) {
            log.info("configuration saved to %s", config.configFile.c_str());
        } else {
            log.error("failed to save configuration to %s", config.configFile.c_str());
        }
    };

    while (true) {
        std::printf("\n");
        std::printf("============================================================\n");
        std::printf("  inmp441_rpi %s - INMP441 I2S microphone recorder\n",
                    appVersion());
        std::printf("============================================================\n");
        std::printf("  Board   : %s\n", audio::I2SController::boardInfo());
        std::printf("  Pins    : SCK=GPIO18  WS=GPIO19  SD=GPIO20\n");
        std::printf("  Rate    : %u Hz\n", config.sampleRate);
        std::printf("  Channel : %s (L/R pin -> %s)\n",
                    config.selectLeftChannel ? "left" : "right",
                    config.selectLeftChannel ? "GND" : "+3V3");
        std::printf("  Gain    : %+.1f dB\n", config.gainDb);
        std::printf("  HPF     : %g Hz high-pass (0 = off)\n", config.hpfHz);
        std::printf("  LPF     : %g Hz low-pass (0 = off)\n", config.lpfHz);
        std::printf("  Format  : %s\n",
                    config.mode == RunMode::kRecordMp3 ? "MP3 (lame)" : "WAV");
        std::printf("  File    : %s\n", config.outputFile.c_str());
        std::printf("  Config  : %s\n", config.configFile.c_str());
        std::printf("------------------------------------------------------------\n");
        std::printf("  1) Duration ....... %g s (min 1)\n", config.durationSeconds);
        std::printf("  2) Channel ........ %s\n",
                    config.selectLeftChannel ? "left" : "right");
        std::printf("  3) Gain ........... %+.1f dB\n", config.gainDb);
        std::printf("  4) Format ......... %s\n",
                    config.mode == RunMode::kRecordMp3 ? "MP3" : "WAV");
        std::printf("  5) Level test ..... live meter for %g s\n", kMenuMeterSeconds);
        std::printf("  6) RECORD\n");
        std::printf("  7) Play output/ over Bluetooth\n");
        std::printf("  8) HPF cutoff ...... %g Hz (0 = off)\n", config.hpfHz);
        std::printf("  9) LPF cutoff ...... %g Hz (0 = off)\n", config.lpfHz);
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
                        persistConfig();
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
                persistConfig();
                break;
            case '3': {
                std::printf("Gain in dB (e.g. 24 close talk, 40 room; +-%.0f max) [%+.1f]> ",
                            core::kMaxGainDb, config.gainDb);
                std::fflush(stdout);
                std::string value;
                std::getline(std::cin, value);
                if (!value.empty()) {
                    char* end = nullptr;
                    const double d = std::strtod(value.c_str(), &end);
                    if (end != value.c_str() && *end == '\0') {
                        config.gainDb = core::clampGainDb(d);
                        if (config.gainDb != d) {
                            std::printf("  (clamped to %+.1f dB)\n", config.gainDb);
                        }
                        persistConfig();
                    } else {
                        std::printf("  (ignored: must be a number)\n");
                    }
                }
                break;
            }
            case '4':
                if (config.mode == RunMode::kRecordMp3) {
                    config.mode = RunMode::kRecordWav;
                    config.recordMp3 = false;
                    config.outputFile = core::defaultOutputName("wav");
                } else {
                    config.mode = RunMode::kRecordMp3;
                    config.recordMp3 = true;
                    config.outputFile = core::defaultOutputName("mp3");
                }
                persistConfig();
                break;
            case '5': {
                Config meterConfig = config;
                meterConfig.meterSeconds = kMenuMeterSeconds;
                runLevelMeter(mic, meterConfig);
                break;
            }
            case '6': {
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
            case '7':
                runPlayerMode(config);
                // Si el usuario salió de la pantalla de reproducción con
                // Ctrl+C, salir también del menú (el flag queda activo).
                if (core::SignalHandler::shouldStop()) {
                    std::printf("Bye.\n");
                    return 0;
                }
                break;
            case '8': {
                std::printf("High-pass cutoff in Hz (0 = off, max %.0f) [%g]> ",
                            core::kMaxHpfHz, config.hpfHz);
                std::fflush(stdout);
                std::string value;
                std::getline(std::cin, value);
                if (!value.empty()) {
                    char* end = nullptr;
                    const double d = std::strtod(value.c_str(), &end);
                    if (end != value.c_str() && *end == '\0' && std::isfinite(d)) {
                        config.hpfHz = core::clampHpfHz(d);
                        if (config.hpfHz != d) {
                            std::printf("  (clamped to %g Hz)\n", config.hpfHz);
                        }
                        log.info("high-pass cutoff set to %g Hz", config.hpfHz);
                        persistConfig();
                    } else {
                        std::printf("  (ignored: must be a number)\n");
                    }
                }
                break;
            }
            case '9': {
                std::printf("Low-pass cutoff in Hz (0 = off, max %.0f) [%g]> ",
                            core::kMaxLpfHz, config.lpfHz);
                std::fflush(stdout);
                std::string value;
                std::getline(std::cin, value);
                if (!value.empty()) {
                    char* end = nullptr;
                    const double d = std::strtod(value.c_str(), &end);
                    if (end != value.c_str() && *end == '\0' && std::isfinite(d)) {
                        config.lpfHz = core::clampLpfHz(d);
                        if (config.lpfHz != d) {
                            std::printf("  (clamped to %g Hz)\n", config.lpfHz);
                        }
                        log.info("low-pass cutoff set to %g Hz", config.lpfHz);
                        persistConfig();
                    } else {
                        std::printf("  (ignored: must be a number)\n");
                    }
                }
                break;
            }
            case '0':
            case 'q':
                std::printf("Bye.\n");
                return 0;
            default:
                std::printf("  (invalid choice; try 1-9, 0/Q)\n");
                break;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    Logger& log = Logger::instance();

    // Cheap pre-scan: --help/--version must not touch the config file, and
    // --config tells us where the persisted settings live.
    bool wantsHelpOrVersion = false;
    std::string configPath = "config.json";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help" || arg == "--version") {
            wantsHelpOrVersion = true;
        } else if (arg == "--config" && i + 1 < argc && argv[i + 1] != nullptr) {
            configPath = argv[i + 1];
        }
    }

    // Persisted settings are loaded as base defaults; CLI options take
    // precedence over them.
    Config base;
    if (!wantsHelpOrVersion) {
        core::loadConfig(base, configPath);
    }

    Config config = core::parseArgs(argc, argv, base);
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
        std::printf("inmp441_rpi %s\n", appVersion());
        return 0;
    }

    // Persist the effective settings when requested (--save-config). The
    // interactive menu also saves on every change.
    if (config.saveConfigRequested) {
        if (!core::saveConfig(config, config.configFile)) {
            return 1;
        }
        log.info("configuration saved to %s", config.configFile.c_str());
    }

    core::SignalHandler::install();

    // RAII microphone handle: the constructor opens the I2S master and throws
    // on failure; the destructor shuts the hardware down when `mic` leaves
    // scope, so nothing needs to be closed explicitly at exit. The player
    // mode does not need the microphone: it only reads output/ and plays it.
    std::unique_ptr<Inmp441_t> mic;
    if (config.mode != RunMode::kPlayer) {
        try {
            mic = std::make_unique<Inmp441_t>(config.sampleRate,
                                              config.selectLeftChannel,
                                              config.driveLrSelectGpio);
        } catch (const std::runtime_error& e) {
            log.error("cannot open microphone: %s", e.what());
            return 1;
        }
    }

    int result = 0;
    switch (config.mode) {
        case RunMode::kMenu:
            result = runMenuMode(*mic, config);
            break;
        case RunMode::kInfo:
            runInfoMode(config);
            break;
        case RunMode::kLevelMeter:
            result = runLevelMeter(*mic, config);
            break;
        case RunMode::kRecordWav:
            result = runRecordMode(*mic, config);
            break;
        case RunMode::kRecordMp3:
            result = runRecordMp3Mode(*mic, config);
            break;
        case RunMode::kDumpRawWords:
            result = runDumpMode(*mic, config);
            break;
        case RunMode::kPlayer:
            result = runPlayerMode(config);
            break;
    }

    // `mic` (unique_ptr) destroys the Inmp441_t here, releasing the hardware.
    return result;
}
