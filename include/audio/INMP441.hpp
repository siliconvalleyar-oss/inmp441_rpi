#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "audio/SampleFormat.hpp"

// Forward declarations (keep ALSA/libgpiod headers out of the public header).
typedef struct _snd_pcm snd_pcm_t;
struct gpiod_chip;
struct gpiod_line;

// Driver for the TDK InvenSense INMP441 MEMS microphone on a Raspberry Pi,
// captured through the kernel I2S driver exposed by ALSA (the
// `dtoverlay=inmp441-bare` overlay). The overlay owns the BCLK/WS/SD pins
// (GPIO 18/19/20) and generates the master clock; the mic delivers 24-bit
// two's-complement audio, left-justified in each 32-bit S32_LE slot, on the
// channel selected by its L/R pin (left when L/R is tied low). GPIO21 (the
// L/R select line) is driven with libgpiod, which does not need root.
//
// This class translates the raw ALSA slots into 24-bit stereo frames.
namespace INMP441 {

// Static, hardware-agnostic description for the --info mode / menu banner.
const char* backendInfo();

// RAII microphone handle: the constructor opens the ALSA capture device and
// selects the mic channel, throwing std::runtime_error on failure; the
// destructor releases the PCM stream and the GPIO line automatically. It is
// meant to be owned through std::unique_ptr, e.g.:
//
//   auto microphone = std::make_unique<INMP441::Inmp441_t>();
//
// and needs no explicit close: everything is released when the object leaves
// scope.
class Inmp441_t {
public:
    // Opens the microphone. `alsaDevice` is the ALSA capture device: the
    // special value "default" auto-detects the first card whose name contains
    // "bare" (the inmp441-bare overlay) instead of relying on the card index.
    // `gpioChip` names the libgpiod chip that carries the L/R select line
    // (gpiochip0 on the Pi 4 / Pi Zero 2W). Throws std::runtime_error if the
    // ALSA stream or the channel selection cannot be initialised.
    explicit Inmp441_t(uint32_t sampleRateHz = 48000,
                       bool selectLeftChannel = true,
                       bool driveLrSelectGpio = true,
                       const std::string& alsaDevice = "default",
                       const std::string& gpioChip = "gpiochip0");
    ~Inmp441_t();

    Inmp441_t(const Inmp441_t&) = delete;
    Inmp441_t& operator=(const Inmp441_t&) = delete;

    // Changes the microphone channel without re-initialising the PCM stream.
    // Drives GPIO21 to match `selectLeftChannel` when `driveLrSelectGpio` is
    // set, otherwise leaves the L/R pin as wired.
    void setChannel(bool selectLeftChannel, bool driveLrSelectGpio);

    // Reads up to `frameCount` stereo frames. Each frame consumes two raw
    // S32_LE slots (left then right). Returns the number of frames actually
    // read (fewer only on an unrecoverable stream error).
    size_t readFrames(audio::AudioFrame* frames, size_t frameCount);

    // Debug helper: reads raw 32-bit slots without interpretation. Each
    // stereo frame yields two slots (left then right).
    size_t readRawWords(uint32_t* words, size_t wordCount);

    // Restores the RX stream: drops any stale buffered samples and re-arms
    // the PCM, so the next readFrames() starts from a clean state. Call
    // before each recording.
    void resetRxStream();

    // Full re-initialisation: closes and re-opens the ALSA stream (and
    // re-applies the L/R select). The kernel driver re-arms the I2S master
    // from scratch, which recovers a recording that would otherwise start on
    // a wedged/stale stream - use it before every capture.
    void resetI2s(uint32_t sampleRateHz, bool selectLeftChannel,
                  bool driveLrSelectGpio);

    uint32_t sampleRateHz() const { return sampleRateHz_; }

    // The resolved ALSA device name actually in use (e.g. "plughw:1,0").
    const std::string& alsaDeviceName() const { return device_; }

    // Number of overruns (xrun) seen on the PCM stream since the last
    // (re)open. An overrun means samples were lost.
    unsigned int xrunCount() const { return xrunCount_; }

private:
    bool openCapture(uint32_t sampleRateHz);
    void closeCapture();
    bool openGpio();
    void closeGpio();
    void applyLrSelect(bool selectLeftChannel, bool driveLrSelectGpio);
    bool readSlots(int32_t* slots, size_t frameCount);

    snd_pcm_t* pcm_ = nullptr;
    std::string device_;        // resolved plughw:card,0 name
    std::string gpioChip_;      // libgpiod chip for the L/R select line
    uint32_t sampleRateHz_ = 0;
    gpiod_chip* chip_ = nullptr;
    gpiod_line* line_ = nullptr;
    bool initialized_ = false;
    unsigned int xrunCount_ = 0;
};

}  // namespace INMP441
