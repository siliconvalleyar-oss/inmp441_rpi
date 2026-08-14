#pragma once

#include <cstddef>
#include <cstdint>

#include "audio/I2SController.hpp"
#include "audio/SampleFormat.hpp"

// Driver for the TDK InvenSense INMP441 MEMS microphone on the BCM2835 PCM/I2S
// peripheral. The mic delivers 24-bit two's-complement audio, left-justified
// in 32-bit slots, on the channel selected by the L/R pin (left when L/R is
// tied low). This class translates the raw hardware slots into 24-bit frames.
namespace INMP441 {

// RAII microphone handle: the constructor opens the I2S master and selects
// the mic channel, throwing std::runtime_error on failure; the destructor
// shuts the hardware down automatically. It is meant to be owned through
// std::unique_ptr, e.g.:
//
//   auto microphone = std::make_unique<INMP441::Inmp441_t>();
//
// and needs no explicit close: everything is released when the object leaves
// scope.
class Inmp441_t {
public:
    // Opens the microphone. Throws std::runtime_error if the I2S master or
    // the channel selection cannot be initialised (details are logged).
    explicit Inmp441_t(uint32_t sampleRateHz = 48000,
                       bool selectLeftChannel = true,
                       bool driveLrSelectGpio = true);
    ~Inmp441_t();

    Inmp441_t(const Inmp441_t&) = delete;
    Inmp441_t& operator=(const Inmp441_t&) = delete;

    // Changes the microphone channel without re-initialising the I2S master.
    // Drives GPIO21 to match `selectLeftChannel` when `driveLrSelectGpio` is
    // set, otherwise leaves the L/R pin as wired.
    void setChannel(bool selectLeftChannel, bool driveLrSelectGpio);

    // Reads up to `frameCount` stereo frames. Each frame consumes two raw
    // slots (left then right). Returns the number of frames actually read.
    size_t readFrames(audio::AudioFrame* frames, size_t frameCount);

    // Debug helper: reads raw 32-bit slots without interpretation.
    size_t readRawWords(uint32_t* words, size_t wordCount);

    // Restores the RX stream: clears FIFO and RX error flags so the next
    // readFrames() starts from a clean state. Call before each recording.
    void resetRxStream();

    // Full re-initialisation of the I2S master: stops and releases the
    // peripheral, then configures it again from scratch (clock, frame sync,
    // GPIOs and L/R select). A recording that starts with a wedged/stale
    // FIFO from the menu idle state is recovered by this - use it before
    // every capture.
    void resetI2s(uint32_t sampleRateHz, bool selectLeftChannel,
                  bool driveLrSelectGpio);

    uint32_t sampleRateHz() const { return sampleRateHz_; }

private:
    audio::I2SController controller_;
    uint32_t sampleRateHz_ = 0;
    bool initialized_ = false;
};

}  // namespace INMP441
