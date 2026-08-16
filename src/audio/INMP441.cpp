#include "audio/INMP441.hpp"

#include <alsa/asoundlib.h>
#include <gpiod.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "audio/AlsaDeviceFinder.hpp"
#include "core/Logger.hpp"

namespace INMP441 {

namespace {

// L/R select line (BCM GPIO 21, physical pin 40).
constexpr unsigned int kGpioLrSel = 21;
// Block size used for the ALSA reads (~21 ms at 48 kHz).
constexpr snd_pcm_uframes_t kPeriodFrames = 1024;
// Frames of the stream: the INMP441 fills one I2S slot per frame and the
// unused slot is silence, so the kernel driver delivers a stereo stream.
constexpr unsigned int kChannels = 2;

}  // namespace

const char* backendInfo() {
    return "Raspberry Pi, I2S via the kernel driver (ALSA overlay "
           "inmp441-bare), L/R select on GPIO21 (libgpiod)";
}

Inmp441_t::Inmp441_t(uint32_t sampleRateHz, bool selectLeftChannel,
                     bool driveLrSelectGpio, const std::string& alsaDevice,
                     const std::string& gpioChip) {
    core::Logger& log = core::Logger::instance();

    if (alsaDevice == "default") {
        if (auto found = audio::FindAlsaDeviceByName("bare")) {
            device_ = *found;
            log.info("ALSA capture device auto-detected: %s", device_.c_str());
        } else {
            device_ = alsaDevice;
            log.warning("no card named 'bare' found (check 'arecord -l'); "
                        "falling back to '%s', which may fail - pass "
                        "--alsa-device plughw:<card>,0 to override",
                        device_.c_str());
        }
    } else {
        device_ = alsaDevice;
    }
    gpioChip_ = gpioChip;

    if (!openCapture(sampleRateHz)) {
        closeCapture();
        throw std::runtime_error("INMP441: cannot open ALSA capture '" +
                                 device_ + "' (see log above)");
    }

    if (driveLrSelectGpio && !openGpio()) {
        closeCapture();
        throw std::runtime_error("INMP441: cannot drive the L/R select GPIO "
                                 "(see log above; use --no-lr-gpio to leave "
                                 "the pin wired manually)");
    }

    applyLrSelect(selectLeftChannel, driveLrSelectGpio);
    initialized_ = true;

    log.info("INMP441 ready: rate=%u Hz, 24-bit I2S via ALSA (%s)",
             sampleRateHz_, device_.c_str());
}

Inmp441_t::~Inmp441_t() {
    if (!initialized_) {
        return;
    }
    closeGpio();
    closeCapture();
    initialized_ = false;
}

bool Inmp441_t::openCapture(uint32_t sampleRateHz) {
    core::Logger& log = core::Logger::instance();

    int err = snd_pcm_open(&pcm_, device_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        log.error("snd_pcm_open('%s') failed: %s", device_.c_str(),
                  snd_strerror(err));
        pcm_ = nullptr;
        return false;
    }

    snd_pcm_hw_params_t* hwParams = nullptr;
    snd_pcm_hw_params_alloca(&hwParams);

    if ((err = snd_pcm_hw_params_any(pcm_, hwParams)) < 0 ||
        (err = snd_pcm_hw_params_set_access(
             pcm_, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
        (err = snd_pcm_hw_params_set_format(
             pcm_, hwParams, SND_PCM_FORMAT_S32_LE)) < 0 ||
        (err = snd_pcm_hw_params_set_channels(
             pcm_, hwParams, kChannels)) < 0) {
        log.error("ALSA hardware config failed: %s", snd_strerror(err));
        return false;
    }

    // The INMP441 delivers 24 useful bits inside each 32-bit S32_LE slot,
    // aligned to the MSB (the low byte is padding); the conversion to 24-bit
    // samples happens in readFrames()/readRawWords().
    unsigned int rate = sampleRateHz;
    err = snd_pcm_hw_params_set_rate_near(pcm_, hwParams, &rate, 0);
    if (err < 0) {
        log.error("ALSA sample rate %u Hz not supported: %s", sampleRateHz,
                  snd_strerror(err));
        return false;
    }
    if (rate != sampleRateHz) {
        log.warning("driver adjusted the sample rate to %u Hz (requested %u Hz)",
                    rate, sampleRateHz);
    }

    snd_pcm_uframes_t period = kPeriodFrames;
    err = snd_pcm_hw_params_set_period_size_near(pcm_, hwParams, &period,
                                                 nullptr);
    if (err < 0) {
        log.warning("could not set period size: %s", snd_strerror(err));
    }

    err = snd_pcm_hw_params(pcm_, hwParams);
    if (err < 0) {
        log.error("snd_pcm_hw_params failed: %s", snd_strerror(err));
        return false;
    }

    err = snd_pcm_prepare(pcm_);
    if (err < 0) {
        log.error("snd_pcm_prepare failed: %s", snd_strerror(err));
        return false;
    }

    sampleRateHz_ = rate;
    xrunCount_ = 0;
    return true;
}

void Inmp441_t::closeCapture() {
    if (pcm_) {
        snd_pcm_drain(pcm_);
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}

bool Inmp441_t::openGpio() {
    core::Logger& log = core::Logger::instance();

    chip_ = gpiod_chip_open_by_name(gpioChip_.c_str());
    if (!chip_) {
        log.error("cannot open %s (libgpiod); is your user in the 'gpio' group?",
                  gpioChip_.c_str());
        return false;
    }

    line_ = gpiod_chip_get_line(chip_, kGpioLrSel);
    if (!line_) {
        log.error("GPIO%u not found on %s", kGpioLrSel, gpioChip_.c_str());
        return false;
    }

    // Requested as an output starting LOW (mic channel left).
    if (gpiod_line_request_output(line_, "inmp441_rpi", 0) < 0) {
        log.error("cannot reserve GPIO%u as an output on %s (is it claimed by "
                  "another process?)", kGpioLrSel, gpioChip_.c_str());
        return false;
    }
    return true;
}

void Inmp441_t::closeGpio() {
    if (line_) {
        gpiod_line_release(line_);
        line_ = nullptr;
    }
    if (chip_) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }
}

void Inmp441_t::applyLrSelect(bool selectLeftChannel, bool driveLrSelectGpio) {
    core::Logger& log = core::Logger::instance();
    if (!driveLrSelectGpio) {
        log.info("L/R select: GPIO%u left untouched (wire it to GND/3V3 yourself)",
                 kGpioLrSel);
        return;
    }
    if (!line_) {
        return;
    }
    gpiod_line_set_value(line_, selectLeftChannel ? 0 : 1);
    log.info("L/R select: GPIO%u driven %s (mic channel %s)", kGpioLrSel,
             selectLeftChannel ? "LOW" : "HIGH",
             selectLeftChannel ? "left" : "right");
}

void Inmp441_t::setChannel(bool selectLeftChannel, bool driveLrSelectGpio) {
    if (!initialized_) {
        return;
    }
    applyLrSelect(selectLeftChannel, driveLrSelectGpio);
}

// Reads up to `frameCount` interleaved stereo frames (2 slots each) into
// `slots`, blocking until data is available. Handles xruns internally.
bool Inmp441_t::readSlots(int32_t* slots, size_t frameCount) {
    if (!pcm_ || frameCount == 0) {
        return false;
    }
    while (true) {
        snd_pcm_sframes_t n =
            snd_pcm_readi(pcm_, slots, static_cast<snd_pcm_uframes_t>(frameCount));
        if (n >= 0) {
            return true;
        }
        if (n == -EPIPE) {
            // Overrun: the kernel buffer filled because we were not reading
            // fast enough; samples were lost. Count it and recover by
            // re-arming the stream.
            ++xrunCount_;
            snd_pcm_prepare(pcm_);
            continue;
        }
        const int err = static_cast<int>(n);
        const int rc = snd_pcm_recover(pcm_, err, 1);
        if (rc < 0) {
            core::Logger::instance().error(
                "ALSA unrecoverable read error: %s", snd_strerror(rc));
            return false;
        }
        continue;
    }
}

size_t Inmp441_t::readFrames(audio::AudioFrame* frames, size_t frameCount) {
    if (!initialized_ || frameCount == 0) {
        return 0;
    }

    std::vector<int32_t> slots(frameCount * kChannels);
    const size_t read = readSlots(slots.data(), frameCount)
                            ? frameCount
                            : 0;
    for (size_t i = 0; i < read; ++i) {
        const uint32_t left = static_cast<uint32_t>(slots[i * 2]);
        const uint32_t right = static_cast<uint32_t>(slots[i * 2 + 1]);
        frames[i].left24 = audio::rawToSample24(left);
        frames[i].right24 = audio::rawToSample24(right);
    }
    return read;
}

size_t Inmp441_t::readRawWords(uint32_t* words, size_t wordCount) {
    if (!initialized_ || wordCount == 0) {
        return 0;
    }
    // readi returns whole frames (2 slots each); round up so the caller can
    // inspect the last odd slot as well.
    const size_t frames = (wordCount + 1) / 2;
    std::vector<int32_t> slots(frames * kChannels);
    if (!readSlots(slots.data(), frames)) {
        return 0;
    }
    size_t copied = 0;
    for (size_t i = 0; i < frames * kChannels && copied < wordCount; ++i) {
        words[copied++] = static_cast<uint32_t>(slots[i]);
    }
    return copied;
}

void Inmp441_t::resetRxStream() {
    if (!initialized_ || !pcm_) {
        return;
    }
    snd_pcm_drop(pcm_);
    snd_pcm_prepare(pcm_);
}

void Inmp441_t::resetI2s(uint32_t sampleRateHz, bool selectLeftChannel,
                         bool driveLrSelectGpio) {
    if (!initialized_) {
        return;
    }
    core::Logger::instance().info(
        "re-opening the ALSA stream before capture (fresh I2S setup)...");
    closeCapture();
    if (!openCapture(sampleRateHz)) {
        initialized_ = false;
        return;
    }
    applyLrSelect(selectLeftChannel, driveLrSelectGpio);
    core::Logger::instance().info("INMP441 re-initialised: rate=%u Hz via ALSA (%s)",
                                  sampleRateHz_, device_.c_str());
}

}  // namespace INMP441
