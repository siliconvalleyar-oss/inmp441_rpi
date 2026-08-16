#include "core/file_utils.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>

namespace i2c_audio {

namespace {

bool HasWavExtension(const std::string& name) {
    if (name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".wav";
}

}  // namespace

std::vector<std::string> ListWavFiles(const std::string& directory) {
    std::vector<std::string> result;

    DIR* dir = opendir(directory.c_str());
    if (!dir) return result;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (HasWavExtension(name)) result.push_back(name);
    }
    closedir(dir);

    std::sort(result.begin(), result.end());
    return result;
}

bool EnsureDirectoryExists(const std::string& directory) {
    struct stat st{};
    if (stat(directory.c_str(), &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    return mkdir(directory.c_str(), 0755) == 0;
}

}  // namespace i2c_audio
