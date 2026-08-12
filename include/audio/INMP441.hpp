#pragma once

#include <cstddef>
#include <cstdint>

#include "audio/I2SController.hpp"
#include "audio/SampleFormat.hpp"

namespace audio {

// Driver for the TDK InvenSense INMP441 MEMS microphone on the BCM2835 PCM/I2S
// peripheral. The mic delivers 24-bit two's-complement audio, left-justified
// in 32-bit slots, on the channel selected by the L/R pin (left when L/R is
// tied low). This class translates the raw hardware slots into 24-bit frames.
class INMP441 {
public:
    INMP441() = default;
    ~INMP441();

    INMP441(const INMP441&) = delete;
    INMP441& operator=(const INMP441&) = delete;

    // Initialises the I2S master and the microphone channel selection.
    bool init(uint32_t sampleRateHz, bool selectLeftChannel, bool driveLrSelectGpio);

    // Stops capture and releases hardware resources.
    void close();

    // Reads up to `frameCount` stereo frames. Each frame consumes two raw
    // slots (left then right). Returns the number of frames actually read.
    size_t readFrames(AudioFrame* frames, size_t frameCount);

    // Debug helper: reads raw 32-bit slots without interpretation.
    size_t readRawWords(uint32_t* words, size_t wordCount);

    uint32_t sampleRateHz() const { return sampleRateHz_; }

private:
    I2SController controller_;
    uint32_t sampleRateHz_ = 0;
    bool initialized_ = false;
};

}  // namespace audio
