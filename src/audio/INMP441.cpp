#include "audio/INMP441.hpp"

#include <stdexcept>

#include "core/Logger.hpp"

namespace INMP441 {

Inmp441_t::Inmp441_t(uint32_t sampleRateHz, bool selectLeftChannel,
                     bool driveLrSelectGpio) {
    // The controller logs the precise failure reason (root check, /dev/mem,
    // clock divider, ...) before returning false.
    if (!controller_.init(sampleRateHz, selectLeftChannel, driveLrSelectGpio)) {
        throw std::runtime_error("INMP441 initialisation failed (see log above)");
    }

    sampleRateHz_ = sampleRateHz;
    initialized_ = true;

    // Drain any stale FIFO contents so the first frame read is frame-aligned.
    uint32_t discard[8];
    controller_.readRaw(discard, 8);

    core::Logger::instance().info("INMP441 ready: rate=%u Hz, 24-bit I2S",
                                  sampleRateHz_);
}

Inmp441_t::~Inmp441_t() {
    if (!initialized_) {
        return;
    }
    controller_.shutdown();
    initialized_ = false;
}

void Inmp441_t::setChannel(bool selectLeftChannel, bool driveLrSelectGpio) {
    if (!initialized_) {
        return;
    }
    controller_.setLrSelect(selectLeftChannel, driveLrSelectGpio);
}

size_t Inmp441_t::readFrames(audio::AudioFrame* frames, size_t frameCount) {
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
        frames[i].left24 = audio::rawToSample24(left);
        frames[i].right24 = audio::rawToSample24(right);
        ++framesRead;
    }
    return framesRead;
}

size_t Inmp441_t::readRawWords(uint32_t* words, size_t wordCount) {
    return controller_.readRaw(words, wordCount);
}

void Inmp441_t::resetRxStream() {
    controller_.resetRx();
}

void Inmp441_t::resetI2s(uint32_t sampleRateHz, bool selectLeftChannel,
                         bool driveLrSelectGpio) {
    if (!initialized_) {
        return;
    }
    core::Logger::instance().info(
        "re-initialising I2S master before capture (fresh mic setup)...");
    // Full power-cycle-equivalent of the peripheral: disable clock/RX, restore
    // GPIOs, then re-route, re-clock and re-enable from scratch.
    controller_.shutdown();
    if (!controller_.init(sampleRateHz, selectLeftChannel, driveLrSelectGpio)) {
        initialized_ = false;
        return;
    }
    sampleRateHz_ = sampleRateHz;

    // Drain any stale FIFO contents so the first frame read is frame-aligned.
    uint32_t discard[8];
    controller_.readRaw(discard, 8);

    core::Logger::instance().info("INMP441 re-initialised: rate=%u Hz, 24-bit I2S",
                                  sampleRateHz_);
}

}  // namespace INMP441
