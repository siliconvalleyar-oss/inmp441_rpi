#pragma once

#include <optional>
#include <string>

namespace audio {

// Walks the ALSA cards registered in the system (via libasound, the same
// thing `arecord -l` does internally) and returns the "plughw:<card>,0"
// device name of the first card whose id or description contains
// `nameHint` (case-insensitive).
//
// This avoids depending on a fixed card number: the I2S overlay
// (dtoverlay=inmp441-bare) can move between indices depending on which
// other audio hardware the Pi detects (HDMI, USB...). Looking it up by
// name instead of index keeps the recorder working across reboots.
//
// Returns std::nullopt when no card matches.
std::optional<std::string> FindAlsaDeviceByName(
    const std::string& nameHint = "bare");

}  // namespace audio
