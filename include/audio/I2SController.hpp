#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

// Low-level driver for the BCM2835 PCM/I2S peripheral, accessed from userspace
// through the bcm2835 library. Generates BCLK/WS in master mode and delivers
// raw 32-bit slots read from the RX FIFO.
//
// This class knows nothing about the INMP441 itself; it simply provides the
// I2S master transport. See INMP441 for the microphone layer.
class I2SController {
public:
    I2SController() = default;
    ~I2SController();

    I2SController(const I2SController&) = delete;
    I2SController& operator=(const I2SController&) = delete;

    // Configures GPIO, the PCM clock and the I2S master. Returns false on
    // failure. Must be run as root.
    bool init(uint32_t sampleRateHz, bool selectLeftChannel, bool driveLrSelectGpio);

    // Stops the peripheral, disables the clock and releases /dev/mem.
    void shutdown();

    // Re-applies the L/R select drive level (mic channel). Only touches the
    // GPIO; does NOT re-initialise the peripheral or clock.
    void setLrSelect(bool selectLeftChannel, bool driveLrSelectGpio);

    // Reads up to `maxWords` raw 32-bit words from the RX FIFO. Returns the
    // number of words actually read (0 on timeout or error).
    size_t readRaw(uint32_t* buffer, size_t maxWords);

    uint32_t sampleRateHz() const { return sampleRateHz_; }

    // Human-readable static info for the --info mode.
    static const char* boardInfo();

private:
    static volatile uint32_t* reg(uint32_t offset);      // PCM peripheral regs
    static volatile uint32_t* cmReg(uint32_t offset);    // clock manager regs
    static uint32_t readReg(uint32_t offset);
    static void writeReg(uint32_t offset, uint32_t value);

    static bool configureClock(uint32_t sampleRateHz, uint32_t* diviOut, uint32_t* divfOut);
    static double oscillatorHz();
    bool configureI2sMaster(uint32_t divi, uint32_t divf);

    bool initialized_ = false;     // fully configured and running
    bool hardwareOpen_ = false;     // bcm2835 mapped (partial-init cleanup)
    uint32_t sampleRateHz_ = 0;
};

}  // namespace audio
