#include "audio/INMP441.hpp"

#include "core/Logger.hpp"

namespace audio {

INMP441::~INMP441() {
    close();
}

bool INMP441::init(uint32_t sampleRateHz, bool selectLeftChannel, bool driveLrSelectGpio) {
    if (!controller_.init(sampleRateHz, selectLeftChannel, driveLrSelectGpio)) {
        return false;
    }

    sampleRateHz_ = sampleRateHz;
    initialized_ = true;

    // Drain any stale FIFO contents so the first frame read is frame-aligned.
    uint32_t discard[8];
    controller_.readRaw(discard, 8);

    core::Logger::instance().info("INMP441 ready: rate=%u Hz, 24-bit I2S",
                                  sampleRateHz_);
    return true;
}

void INMP441::close() {
    if (!initialized_) {
        return;
    }
    controller_.shutdown();
    initialized_ = false;
}

void INMP441::setChannel(bool selectLeftChannel, bool driveLrSelectGpio) {
    if (!initialized_) {
        return;
    }
    controller_.setLrSelect(selectLeftChannel, driveLrSelectGpio);
}

size_t INMP441::readFrames(AudioFrame* frames, size_t frameCount) {
    if (!initialized_ || frameCount == 0) {
        return 0;
    }

    size_t framesRead = 0;
    for (size_t i = 0; i < frameCount; ++i) {
        uint32_t left = 0;
        uint32_t right = 0;
        if (controller_.readRaw(&left, 1) != 1) {
            break;
        }
        if (controller_.readRaw(&right, 1) != 1) {
            break;
        }
        frames[i].left24 = rawToSample24(left);
        frames[i].right24 = rawToSample24(right);
        ++framesRead;
    }
    return framesRead;
}

size_t INMP441::readRawWords(uint32_t* words, size_t wordCount) {
    return controller_.readRaw(words, wordCount);
}

}  // namespace audio
