#pragma once

#include <string>

// Forward declarations de libgpiod (evita incluir gpiod.h en el header)
struct gpiod_chip;
struct gpiod_line;

namespace i2c_audio {

enum class MicChannel { Left = 0, Right = 1 };

// Controla el pin L/R del INMP441 (GPIO21 en el cableado por defecto).
// El INMP441 solo lee este pin de forma estática (no es parte del protocolo
// I2S en tiempo real): en LOW la muestra sale en el slot izquierdo, en HIGH
// en el slot derecho. Por eso alcanza con fijarlo una sola vez al iniciar.
class GpioChannelSelect {
public:
    // gpio_line: número de línea GPIO (BCM), p.ej. 21.
    // chip_name: nombre del chip gpiochip, p.ej. "gpiochip0" (Pi4/Pi Zero 2W).
    GpioChannelSelect(unsigned int gpio_line,
                        const std::string& chip_name = "gpiochip0");
    ~GpioChannelSelect();

    GpioChannelSelect(const GpioChannelSelect&) = delete;
    GpioChannelSelect& operator=(const GpioChannelSelect&) = delete;

    bool Init();
    bool SetChannel(MicChannel channel);
    bool IsReady() const { return ready_; }

private:
    unsigned int gpio_line_;
    std::string chip_name_;
    gpiod_chip* chip_ = nullptr;
    gpiod_line* line_ = nullptr;
    bool ready_ = false;
};

}  // namespace i2c_audio
