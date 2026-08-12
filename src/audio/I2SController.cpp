#include "audio/I2SController.hpp"

#include <bcm2835.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <unistd.h>

#include "core/Logger.hpp"

namespace audio {

namespace {

// ---------------------------------------------------------------------------
// PCM/I2S peripheral registers. Offsets are relative to the PCM base
// (0x203000 inside the peripherals block). Bit layouts follow the official
// Broadcom bcm2835-i2s kernel driver (sound/soc/bcm/bcm2835-i2s.c).
// ---------------------------------------------------------------------------
constexpr uint32_t kPcmBase = 0x203000;

constexpr uint32_t kRegCs = 0x00;
constexpr uint32_t kRegFifo = 0x04;
constexpr uint32_t kRegMode = 0x08;
constexpr uint32_t kRegRxc = 0x0C;
constexpr uint32_t kRegTxc = 0x10;
constexpr uint32_t kRegDreq = 0x14;

// CS register bits.
constexpr uint32_t kCsStandby = 1U << 25;  // set to disable standby
constexpr uint32_t kCsSync = 1U << 24;
constexpr uint32_t kCsRxSex = 1U << 23;  // receive sign extension
constexpr uint32_t kCsRxf = 1U << 22;    // RX FIFO full
constexpr uint32_t kCsTxe = 1U << 21;    // TX FIFO empty
constexpr uint32_t kCsRxd = 1U << 20;    // RX FIFO contains data
constexpr uint32_t kCsTxd = 1U << 19;    // TX FIFO can accept data
constexpr uint32_t kCsRxr = 1U << 18;    // RX threshold reached
constexpr uint32_t kCsTxw = 1U << 17;    // TX threshold reached
constexpr uint32_t kCsRxErr = 1U << 16;  // receive error flag
constexpr uint32_t kCsTxErr = 1U << 15;  // transmit error flag
constexpr uint32_t kCsRxSync = 1U << 14; // receive sync lost
constexpr uint32_t kCsTxSync = 1U << 13; // transmit sync lost
constexpr uint32_t kCsDmaEn = 1U << 9;   // DMA enable
constexpr uint32_t kCsRxThrMask = 3U << 7;
constexpr uint32_t kCsTxThrMask = 3U << 5;
constexpr uint32_t kCsRxClr = 1U << 4;   // clear RX FIFO
constexpr uint32_t kCsTxClr = 1U << 3;   // clear TX FIFO
constexpr uint32_t kCsTxOn = 1U << 2;    // transmit on
constexpr uint32_t kCsRxOn = 1U << 1;    // receive on
constexpr uint32_t kCsEn = 1U << 0;      // enable

// MODE register bits.
constexpr uint32_t kModeClkDis = 1U << 28;
constexpr uint32_t kModePdmN = 1U << 27;
constexpr uint32_t kModePdmE = 1U << 26;
constexpr uint32_t kModeFrxp = 1U << 25;  // frame packed RX
constexpr uint32_t kModeFtxp = 1U << 24;  // frame packed TX
constexpr uint32_t kModeClkM = 1U << 23;  // set = BCLK slave, clear = master
constexpr uint32_t kModeClkI = 1U << 22;  // clock invert (sampling edge)
constexpr uint32_t kModeFsm = 1U << 21;   // set = WS slave, clear = master
constexpr uint32_t kModeFsi = 1U << 20;   // frame sync invert
constexpr uint32_t kModeFlenShift = 10;   // frame length - 1 (10 bits)
constexpr uint32_t kModeFlenMask = 0x3FF;
constexpr uint32_t kModeFslenMask = 0x3FF;  // frame sync length (10 bits)

// RXC / TXC channel fields (each channel occupies half of the register).
constexpr uint32_t kChWex = 1U << 15;       // extended word width
constexpr uint32_t kChEn = 1U << 14;        // channel enable
constexpr uint32_t kChPosShift = 4;         // channel data position
constexpr uint32_t kChWidMask = 0xF;        // word width = (data_length - 8)
constexpr uint32_t kCh1Shift = 16;          // channel 1 in the upper half
constexpr uint32_t kCh2Shift = 0;           // channel 2 in the lower half

// ---------------------------------------------------------------------------
// Clock manager (CM). Offsets relative to 0x101000 inside the peripherals
// block. Follows the bcm2835 kernel clock driver (drivers/clk/bcm/).
// ---------------------------------------------------------------------------
constexpr uint32_t kClockBase = 0x101000;
constexpr uint32_t kRegCmPcmCtl = 0x098;
constexpr uint32_t kRegCmPcmDiv = 0x09C;

constexpr uint32_t kCmPassword = 0x5A000000;
constexpr uint32_t kCmEnable = 1U << 4;
constexpr uint32_t kCmBusy = 1U << 7;
constexpr uint32_t kCmFrac = 1U << 9;      // fractional (MASH) divider enable
constexpr uint32_t kCmSrcMask = 0xF;
constexpr uint32_t kCmSrcOsc = 1;          // 19.2 MHz crystal oscillator
constexpr uint32_t kCmDivShift = 12;       // DIVI position in CM_PCMDIV
constexpr uint32_t kCmDivFracMask = 0xFFF; // 12-bit fractional part

// ---------------------------------------------------------------------------
// GPIO / wiring (BCM2835 GPIO numbers).
// ---------------------------------------------------------------------------
constexpr uint8_t kGpioPcmClk = 18;  // PCM_CLK -> SCK / BCLK
constexpr uint8_t kGpioPcmFs = 19;   // PCM_FS  -> WS / LRCK
constexpr uint8_t kGpioPcmDin = 20;  // PCM_DIN -> SD / data
constexpr uint8_t kGpioLrSel = 21;   // INMP441 L/R select (physical pin 40)

// I2S: 2 slots of 32 bits => 64 bits per frame (FLEN = 63).
constexpr uint32_t kFrameBits = 64;
// 50% duty cycle frame sync => 32 bits (FSLEN).
constexpr uint32_t kFrameSyncBits = 32;
// I2S data starts one bit after the frame-sync edge.
constexpr uint32_t kDataDelayBits = 1;
// 32-bit slot width, encoded as (width - 8) per the kernel convention.
constexpr uint32_t kWordWidthCode = 32 - 8;
// Master clock input (crystal oscillator) used for the PCM clock divider.
// BCM2835/6/7 (Pi 1..Zero 2W, 3) use a 19.2 MHz crystal; BCM2711 (Pi 4 /
// CM4 / Pi 400) and BCM2712 (Pi 5) use a 54 MHz crystal.
constexpr double kOscillatorHzLegacy = 19200000.0;
constexpr double kOscillatorHzModern = 54000000.0;
constexpr uint32_t kBitsPerFrame = 64;

constexpr uint32_t kBusyWaitMs = 100;

}  // namespace

volatile uint32_t* I2SController::reg(uint32_t offset) {
    return reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<uint8_t*>(bcm2835_peripherals) + kPcmBase + offset);
}

volatile uint32_t* I2SController::cmReg(uint32_t offset) {
    return reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<uint8_t*>(bcm2835_peripherals) + kClockBase + offset);
}

uint32_t I2SController::readReg(uint32_t offset) {
    return bcm2835_peri_read(reg(offset));
}

void I2SController::writeReg(uint32_t offset, uint32_t value) {
    bcm2835_peri_write(reg(offset), value);
}

I2SController::~I2SController() {
    shutdown();
}

double I2SController::oscillatorHz() {
    // Read the board model from the device tree; the clock manager crystal
    // differs between the old (19.2 MHz) and new (54 MHz) Raspberry Pis.
    FILE* modelFile = std::fopen("/proc/device-tree/model", "r");
    if (modelFile != nullptr) {
        char model[128] = {0};
        const size_t n = std::fread(model, 1, sizeof(model) - 1, modelFile);
        std::fclose(modelFile);
        model[n] = '\0';
        if (std::strstr(model, "4") != nullptr || std::strstr(model, "5") != nullptr) {
            return kOscillatorHzModern;
        }
    }
    return kOscillatorHzLegacy;
}

bool I2SController::configureClock(uint32_t sampleRateHz, uint32_t* diviOut,
                                   uint32_t* divfOut) {
    // BCLK = sample_rate * 64; divider = oscillator / BCLK.
    const double divisor = oscillatorHz() / (static_cast<double>(sampleRateHz) * kBitsPerFrame);
    const uint32_t divi = static_cast<uint32_t>(divisor);
    double frac = divisor - static_cast<double>(divi);
    uint32_t divf = static_cast<uint32_t>(frac * 4096.0 + 0.5);
    if (divf >= 4096) {
        divf = 0;
    }

    if (divi < 2) {
        // Fractional (MASH) dividers require an integer part >= 2.
        core::Logger::instance().error(
            "unsupported clock divider %u (sample rate %u Hz out of range)",
            divi, sampleRateHz);
        return false;
    }

    const uint32_t srcField = kCmSrcOsc;
    volatile uint32_t* ctl = cmReg(kRegCmPcmCtl);
    volatile uint32_t* div = cmReg(kRegCmPcmDiv);

    // 1) Disable the clock and wait for BUSY to clear.
    bcm2835_peri_write(ctl, kCmPassword | srcField);
    for (uint32_t i = 0; i < kBusyWaitMs; ++i) {
        if ((bcm2835_peri_read(ctl) & kCmBusy) == 0) {
            break;
        }
        bcm2835_delay(1);
    }

    // 2) Program the integer + fractional divider.
    bcm2835_peri_write(div, kCmPassword | (divi << kCmDivShift) | divf);

    // 3) Re-enable with the fractional flag when needed.
    bcm2835_peri_write(ctl, kCmPassword | srcField | (divf != 0 ? kCmFrac : 0U) | kCmEnable);

    *diviOut = divi;
    *divfOut = divf;
    return true;
}

bool I2SController::configureI2sMaster(uint32_t divi, uint32_t divf) {
    (void)divi;
    (void)divf;

    // MODE: master for BCLK and WS, sampling on the rising edge (CLKI), frame
    // start on the falling edge (FSI, I2S), 64-bit frame, 32-bit sync.
    const uint32_t mode = ((kFrameBits - 1) << kModeFlenShift) |
                          (kFrameSyncBits & kModeFslenMask) | kModeClkI | kModeFsi;
    writeReg(kRegMode, mode);

    // RXC / TXC: both channels enabled, 32-bit width, data starts one bit
    // after the frame-sync edge (I2S delay).
    const uint32_t channel = kChEn | kChWex | (kWordWidthCode & kChWidMask);
    const uint32_t ch1 = channel | ((kDataDelayBits) << kChPosShift);
    const uint32_t ch2 = channel | ((kFrameSyncBits + kDataDelayBits) << kChPosShift);
    const uint32_t rxcTxc = (ch1 << kCh1Shift) | (ch2 << kCh2Shift);
    writeReg(kRegRxc, rxcTxc);
    writeReg(kRegTxc, rxcTxc);

    // Enable the block, disable standby, clear both FIFOs, enable sign
    // extension. Clock must already be running for the FIFO clear to work.
    writeReg(kRegCs, kCsEn | kCsStandby | kCsRxSex | kCsRxClr | kCsTxClr);
    bcm2835_delay(1);

    // Start receiving.
    writeReg(kRegCs, readReg(kRegCs) | kCsRxOn);
    return true;
}

bool I2SController::init(uint32_t sampleRateHz, bool selectLeftChannel,
                         bool driveLrSelectGpio) {
    core::Logger& log = core::Logger::instance();

    if (geteuid() != 0) {
        log.error("must be run as root (bcm2835 requires /dev/mem access)");
        return false;
    }
    if (!bcm2835_init()) {
        log.error("bcm2835_init() failed (is /dev/mem accessible?)");
        return false;
    }

    // Drive the INMP441 L/R select line (physical pin 40 = GPIO 21).
    if (driveLrSelectGpio) {
        bcm2835_gpio_fsel(kGpioLrSel, BCM2835_GPIO_FSEL_OUTP);
        bcm2835_gpio_write(kGpioLrSel, selectLeftChannel ? LOW : HIGH);
        log.info("L/R select: GPIO%d driven %s (mic channel %s)",
                 kGpioLrSel, selectLeftChannel ? "LOW" : "HIGH",
                 selectLeftChannel ? "left" : "right");
    } else {
        log.info("L/R select: GPIO%d left untouched (wire it to GND/3V3 yourself)",
                 kGpioLrSel);
    }

    // Route GPIO 18/19/20 to the PCM peripheral (ALT0).
    bcm2835_gpio_fsel(kGpioPcmClk, BCM2835_GPIO_FSEL_ALT0);
    bcm2835_gpio_fsel(kGpioPcmFs, BCM2835_GPIO_FSEL_ALT0);
    bcm2835_gpio_fsel(kGpioPcmDin, BCM2835_GPIO_FSEL_ALT0);

    uint32_t divi = 0;
    uint32_t divf = 0;
    if (!configureClock(sampleRateHz, &divi, &divf)) {
        shutdown();
        return false;
    }

    if (!configureI2sMaster(divi, divf)) {
        shutdown();
        return false;
    }

    sampleRateHz_ = sampleRateHz;
    initialized_ = true;

    log.info("I2S master ready: rate=%u Hz, BCLK=%.3f MHz, div=%u.%04u (xosc %.1f MHz)",
             sampleRateHz_,
             static_cast<double>(sampleRateHz_) * kBitsPerFrame / 1000000.0,
             divi, (divf * 10000) / 4096, oscillatorHz() / 1000000.0);
    return true;
}

void I2SController::setLrSelect(bool selectLeftChannel, bool driveLrSelectGpio) {
    if (!initialized_) {
        return;
    }
    if (driveLrSelectGpio) {
        bcm2835_gpio_fsel(kGpioLrSel, BCM2835_GPIO_FSEL_OUTP);
        bcm2835_gpio_write(kGpioLrSel, selectLeftChannel ? LOW : HIGH);
        core::Logger::instance().info("L/R select: GPIO%d driven %s (mic channel %s)",
                                      kGpioLrSel,
                                      selectLeftChannel ? "LOW" : "HIGH",
                                      selectLeftChannel ? "left" : "right");
    } else {
        core::Logger::instance().info("L/R select: GPIO%d left untouched (wire it to GND/3V3 yourself)",
                                      kGpioLrSel);
    }
}

void I2SController::shutdown() {
    if (!initialized_) {
        return;
    }

    // Stop reception and disable the module.
    writeReg(kRegCs, 0);

    // Disable the PCM clock.
    volatile uint32_t* ctl = cmReg(kRegCmPcmCtl);
    bcm2835_peri_write(ctl, kCmPassword | kCmSrcOsc);
    for (uint32_t i = 0; i < kBusyWaitMs; ++i) {
        if ((bcm2835_peri_read(ctl) & kCmBusy) == 0) {
            break;
        }
        bcm2835_delay(1);
    }

    // Restore GPIOs to a safe state.
    bcm2835_gpio_fsel(kGpioPcmClk, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(kGpioPcmFs, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(kGpioPcmDin, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(kGpioLrSel, BCM2835_GPIO_FSEL_INPT);

    bcm2835_close();
    initialized_ = false;
}

size_t I2SController::readRaw(uint32_t* buffer, size_t maxWords) {
    if (!initialized_ || maxWords == 0) {
        return 0;
    }

    // Rate-limit the RX timeout warning to avoid log flooding.
    static auto lastRxWarn = std::chrono::steady_clock::time_point{};

    size_t words = 0;
    while (words < maxWords) {
        // Wait for at least one word in the RX FIFO.
        uint32_t spins = 0;
        while ((readReg(kRegCs) & kCsRxd) == 0) {
            if (++spins > 1000000) {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastRxWarn > std::chrono::seconds(1)) {
                    lastRxWarn = now;
                    core::Logger::instance().warning("RX FIFO timeout waiting for data");
                }
                return words;
            }
        }
        buffer[words++] = bcm2835_peri_read(reg(kRegFifo));
    }
    return words;
}

const char* I2SController::boardInfo() {
    return "BCM2835 family (Raspberry Pi Zero/1/2/3/Zero 2W), PCM/I2S via bcm2835";
}

}  // namespace audio
