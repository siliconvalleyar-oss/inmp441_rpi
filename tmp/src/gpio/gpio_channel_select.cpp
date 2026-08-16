#include "gpio/gpio_channel_select.hpp"

#include <gpiod.h>

#include <iostream>

namespace i2c_audio {

GpioChannelSelect::GpioChannelSelect(unsigned int gpio_line,
                                        const std::string& chip_name)
    : gpio_line_(gpio_line), chip_name_(chip_name) {}

GpioChannelSelect::~GpioChannelSelect() {
    if (line_) gpiod_line_release(line_);
    if (chip_) gpiod_chip_close(chip_);
}

bool GpioChannelSelect::Init() {
    chip_ = gpiod_chip_open_by_name(chip_name_.c_str());
    if (!chip_) {
        std::cerr << "GpioChannelSelect: no se pudo abrir " << chip_name_
                   << " (¿faltan permisos? probá con sudo o agregá tu "
                   << "usuario al grupo 'gpio')\n";
        return false;
    }

    line_ = gpiod_chip_get_line(chip_, gpio_line_);
    if (!line_) {
        std::cerr << "GpioChannelSelect: no existe la línea GPIO"
                   << gpio_line_ << " en " << chip_name_ << "\n";
        return false;
    }

    // Se pide como salida, arrancando en LOW (canal izquierdo).
    if (gpiod_line_request_output(line_, "inmp441_recorder", 0) < 0) {
        std::cerr << "GpioChannelSelect: no se pudo reservar GPIO"
                   << gpio_line_ << " como salida\n";
        return false;
    }

    ready_ = true;
    return true;
}

bool GpioChannelSelect::SetChannel(MicChannel channel) {
    if (!ready_ || !line_) return false;
    const int value = (channel == MicChannel::Left) ? 0 : 1;
    if (gpiod_line_set_value(line_, value) < 0) {
        std::cerr << "GpioChannelSelect: fallo al escribir GPIO"
                   << gpio_line_ << "\n";
        return false;
    }
    return true;
}

}  // namespace i2c_audio
