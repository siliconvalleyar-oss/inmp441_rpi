#include "audio/AlsaDeviceFinder.hpp"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cctype>

namespace audio {

namespace {

std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool ContainsCaseInsensitive(const std::string& haystack,
                             const std::string& needle) {
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

}  // namespace

std::optional<std::string> FindAlsaDeviceByName(const std::string& nameHint) {
    int card = -1;

    while (snd_card_next(&card) >= 0 && card >= 0) {
        const std::string ctlName = "hw:" + std::to_string(card);

        snd_ctl_t* ctl = nullptr;
        if (snd_ctl_open(&ctl, ctlName.c_str(), 0) < 0) {
            continue;
        }

        snd_ctl_card_info_t* info = nullptr;
        snd_ctl_card_info_alloca(&info);

        std::optional<std::string> result;
        if (snd_ctl_card_info(ctl, info) >= 0) {
            const char* id = snd_ctl_card_info_get_id(info);
            const char* name = snd_ctl_card_info_get_name(info);
            const std::string idStr = id ? id : "";
            const std::string nameStr = name ? name : "";

            if (ContainsCaseInsensitive(idStr, nameHint) ||
                ContainsCaseInsensitive(nameStr, nameHint)) {
                result = "plughw:" + std::to_string(card) + ",0";
            }
        }

        snd_ctl_close(ctl);
        if (result) {
            return result;
        }
    }

    return std::nullopt;
}

}  // namespace audio
