//////////////////////////////////////////////////////////////////
//
//                  oled_display.cpp
//
// Descripción: Implementación de `OLED::OledDisplay` (SSD1306 128x32
//              por I2C 0x3C, driver SSD1306_OLED_RPI de Gavin Lyons).
//
//              IMPORTANTE: este módulo NO gestiona el ciclo de vida
//              de bcm2835 (bcm2835_init/close). El mapeo de /dev/mem
//              lo posee audio::I2SController (el micrófono), que lo
//              cierra al salir del proceso. Por eso aquí nunca se
//              llama a bcm2835_close(): cerrar el mapeo mientras el
//              micrófono sigue abierto rompería la captura I2S.
//
//              La clase es "tolerante a fallos": si no hay display
//              (sin hardware o sin root) la app sigue en consola.
//
//////////////////////////////////////////////////////////////////

#include "oled_display.hpp"

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#if defined(__arm__) || defined(__aarch64__)
#include <bcm2835.h>
#include <SSD1306_OLED.hpp>
#endif

#include "core/Logger.hpp"

namespace OLED {

namespace {
// Dirección I2C del display SSD1306 (0x3C).
constexpr std::uint8_t kI2cAddress = 0x3C;

// Sondeo acotado del bus I2C con el driver del kernel (/dev/i2c-1): si el
// display no responde ACK, no se inicializa la librería SSD1306, que con
// hardware ausente se queda en un bucle de espera infinito y cuelga la app.
// El driver del kernel acota la transacción en el tiempo, así que esto
// nunca se bloquea.
bool i2cProbe(std::uint8_t address) {
    const int fd = ::open("/dev/i2c-1", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    bool ok = false;
    if (::ioctl(fd, I2C_SLAVE, address) == 0) {
        const char cmd = 0x00;  // byte nulo: solo comprueba el ACK
        ok = ::write(fd, &cmd, 1) == 1;
    }
    ::close(fd);
    return ok;
}
}  // namespace

OledDisplay::OledDisplay()
    : oled_(nullptr) {
#if defined(__arm__) || defined(__aarch64__)
    oled_ = new SSD1306(kWidth, kHeight);
#endif
}

OledDisplay::~OledDisplay() {
    shutdown();
#if defined(__arm__) || defined(__aarch64__)
    delete oled_;
#endif
    oled_ = nullptr;
}

bool OledDisplay::init() {
#if !defined(__arm__) && !defined(__aarch64__)
    core::Logger::instance().warning(
        "OLED display not available on this platform (Raspberry Pi required)");
    return false;
#else
    if (oled_ == nullptr) return false;

    // Si no hay display en el bus, la librería SSD1306 se queda esperando
    // ACK para siempre (bucle de 100 ms sin salida) y la app se congela en
    // el modo reproductor. Sondeo acotado antes de inicializarla.
    if (!i2cProbe(kI2cAddress)) {
        core::Logger::instance().warning(
            "OLED: no display at 0x%02X on /dev/i2c-1; continuing without display",
            kI2cAddress);
        return false;
    }

    // bcm2835 ya está inicializado por el controlador I2S; bcm2835_init()
    // es idempotente, así que llamarlo aquí es seguro.
    if (!bcm2835_init()) {
        core::Logger::instance().warning(
            "OLED: bcm2835_init() failed (needs root); continuing without display");
        return false;
    }

    if (!oled_->OLED_I2C_ON()) {
        core::Logger::instance().warning(
            "OLED: I2C bus unavailable; continuing without display");
        return false;
    }

    oled_->OLEDbegin(BCM2835_I2C_CLOCK_DIVIDER_626, 0x3C, false);

    if (!oled_->OLEDSetBufferPtr(kWidth, kHeight, buffer_, sizeof(buffer_))) {
        core::Logger::instance().warning(
            "OLED: framebuffer setup failed; continuing without display");
        oled_->OLED_I2C_OFF();
        return false;
    }

    ready_ = true;
    oled_->OLEDclearBuffer();
    oled_->OLEDupdate();
    core::Logger::instance().info("OLED display ready (128x32 I2C 0x3C)");
    return true;
#endif
}

void OledDisplay::clear() {
#if defined(__arm__) || defined(__aarch64__)
    if (!ready_) return;
    oled_->OLEDclearBuffer();
    oled_->OLEDupdate();
#endif
}

void OledDisplay::showTrack(int index, int total,
                            const std::string& name,
                            bool playing, bool paused,
                            int scrollOffset, int volumePercent,
                            const std::string& version,
                            double positionSeconds,
                            double durationSeconds) {
#if defined(__arm__) || defined(__aarch64__)
    if (!ready_) return;

    oled_->OLEDclearBuffer();
    oled_->setFontNum(OLEDFont_Default);
    oled_->setTextSize(1);
    oled_->setTextColor(WHITE);

    // Fila 0: estado + contador  "> 03/12 V:85"
    char statusLine[32];
    const char* icon = paused ? "||" : (playing ? ">" : " ");
    if (volumePercent >= 0) {
        std::snprintf(statusLine, sizeof(statusLine), "%s %02d/%02d V:%d%%",
                      icon, index + 1, total, volumePercent);
    } else {
        std::snprintf(statusLine, sizeof(statusLine), "%s %02d/%02d",
                      icon, index + 1, total);
    }
    drawLine(0, statusLine);

    // Fila 1: nombre del tema, con marquee lento solo si no cabe.
    std::string line = name;
    const int span = static_cast<int>(line.size()) - kMaxNameChars + 1;
    if (span > 1) {
        const int frames = scrollOffset - kMarqueeDelayFrames;
        if (frames >= 0) {
            const int marqueeTotal = 2 * span;
            int pos = (frames / kMarqueeStepFrames) % marqueeTotal;
            if (pos >= span) pos = marqueeTotal - 1 - pos;
            line = line.substr(pos, kMaxNameChars);
        }
    }
    drawLine(1, line);

    if (playing) {
        // Fila 2: barra de progreso ASCII (ancho completo de la fila).
        int filled = (durationSeconds > 0.0)
            ? static_cast<int>(positionSeconds / durationSeconds *
                               kMaxNameChars + 0.5)
            : 0;
        if (filled < 0) filled = 0;
        if (filled > kMaxNameChars) filled = kMaxNameChars;

        std::string bar;
        bar.reserve(kMaxNameChars);
        for (int i = 0; i < kMaxNameChars; ++i) {
            bar += (i < filled) ? '#' : '-';
        }
        drawLine(2, bar);

        // Fila 3: tiempo transcurrido / total + versión de la app.
        char timeLine[32];
        const int pm = static_cast<int>(positionSeconds) / 60;
        const int ps = static_cast<int>(positionSeconds) % 60;
        if (durationSeconds > 0.0) {
            const int tm = static_cast<int>(durationSeconds) / 60;
            const int ts = static_cast<int>(durationSeconds) % 60;
            std::snprintf(timeLine, sizeof(timeLine), "%d:%02d/%d:%02d v%s",
                          pm, ps, tm, ts, version.c_str());
        } else {
            std::snprintf(timeLine, sizeof(timeLine), "%d:%02d/--:-- v%s",
                          pm, ps, version.c_str());
        }
        drawLine(3, timeLine);
    } else {
        // Parado: ayuda de teclas + versión.
        drawLine(2, "P:pausa  Q:salir");
        std::string vline = version.empty() ? std::string("v?") : ("v" + version);
        drawLine(3, vline);
    }

    oled_->OLEDupdate();
#endif
}

void OledDisplay::showMessage(const std::string& line1, const std::string& line2) {
#if defined(__arm__) || defined(__aarch64__)
    if (!ready_) return;

    oled_->OLEDclearBuffer();
    oled_->setFontNum(OLEDFont_Default);
    oled_->setTextSize(1);
    oled_->setTextColor(WHITE);
    drawLine(0, line1);
    if (!line2.empty()) drawLine(1, line2);
    oled_->OLEDupdate();
#endif
}

void OledDisplay::shutdown() {
#if defined(__arm__) || defined(__aarch64__)
    if (!ready_ || oled_ == nullptr) return;
    oled_->OLEDclearBuffer();
    oled_->OLEDupdate();
    oled_->OLEDPowerDown();
    oled_->OLED_I2C_OFF();
    // Sin bcm2835_close(): el mapeo lo cierra I2SController al salir.
    ready_ = false;
#endif
}

void OledDisplay::drawLine(int y, const std::string& text) {
#if defined(__arm__) || defined(__aarch64__)
    std::string clipped = text;
    if (static_cast<int>(clipped.size()) > kMaxNameChars) {
        clipped = clipped.substr(0, kMaxNameChars);
    }
    oled_->setCursor(0, y * 8);
    oled_->print(clipped);
#endif
}

} // namespace OLED
