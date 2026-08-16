#pragma once

#include <optional>
#include <string>

namespace i2c_audio {

// Recorre las tarjetas ALSA registradas en el sistema (vía libasound,
// equivalente a lo que hace 'arecord -l' por dentro) y devuelve el nombre
// de dispositivo 'plughw:<card>,0' de la primera tarjeta cuyo id o
// descripción contenga name_hint (case-insensitive).
//
// Sirve para no depender del número de card, que puede cambiar entre
// reinicios según qué otro hardware de audio detecte la Pi (HDMI, USB,
// etc.) — se busca por nombre en vez de por índice fijo.
//
// Devuelve std::nullopt si no se encontró ninguna tarjeta que matchee.
std::optional<std::string> FindAlsaDeviceByName(
    const std::string& name_hint = "bare");

}  // namespace i2c_audio
