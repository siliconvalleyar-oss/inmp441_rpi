#include "audio/alsa_device_finder.hpp"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cctype>

namespace i2c_audio {

namespace {

std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return out;
}

bool ContainsCaseInsensitive(const std::string& haystack,
                               const std::string& needle) {
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

}  // namespace

std::optional<std::string> FindAlsaDeviceByName(const std::string& name_hint) {
    int card = -1;

    while (snd_card_next(&card) >= 0 && card >= 0) {
        const std::string ctl_name = "hw:" + std::to_string(card);

        snd_ctl_t* ctl = nullptr;
        if (snd_ctl_open(&ctl, ctl_name.c_str(), 0) < 0) continue;

        snd_ctl_card_info_t* info = nullptr;
        snd_ctl_card_info_alloca(&info);

        std::optional<std::string> result;
        if (snd_ctl_card_info(ctl, info) >= 0) {
            const char* id = snd_ctl_card_info_get_id(info);
            const char* name = snd_ctl_card_info_get_name(info);
            const std::string id_str = id ? id : "";
            const std::string name_str = name ? name : "";

            if (ContainsCaseInsensitive(id_str, name_hint) ||
                ContainsCaseInsensitive(name_str, name_hint)) {
                result = "plughw:" + std::to_string(card) + ",0";
            }
        }

        snd_ctl_close(ctl);
        if (result) return result;
    }

    return std::nullopt;
}

}  // namespace i2c_audio
