//////////////////////////////////////////////////////////////////
//
//                  track_list.cpp
//
// Descripción: Implementación de la clase `TrackList`. Escanea un
//              directorio en busca de archivos `.wav` y `.mp3`
//              (sin distinguir mayúsculas/minúsculas), los ordena
//              alfabéticamente y expone los nombres mostrables.
//
//              Se usa dirent (POSIX) en vez de std::filesystem para
//              no depender del enlazado de -lstdc++fs en compiladores
//              antiguos de Raspberry Pi OS.
//
// Autor: lion
// Fecha: 2024 Octb (refactorizado)
//
//////////////////////////////////////////////////////////////////

#include "track_list.hpp"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

#include "core/Logger.hpp"

namespace SOUND_LIST {

namespace {

// True si el nombre de archivo termina en .wav o .mp3 (cualquier caso).
bool hasAudioExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;

    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".wav" || ext == ".mp3";
}

}  // namespace

TrackList::TrackList(const std::string& directory)
    : dir_(directory) {}

bool TrackList::load() {
    paths_.clear();

    // La app crea output/ al grabar; si aún no existe, créalo para que la
    // lista quede simplemente vacía ("no playable files") en vez de fallar.
    ::mkdir(dir_.c_str(), 0777);  // EEXIST y otros errores se ignoran

    DIR* dir = ::opendir(dir_.c_str());
    if (dir == nullptr) {
        core::Logger::instance().warning(
            "cannot open track directory '%s'", dir_.c_str());
        return true;  // lista vacía, no es un error fatal
    }

    // Escaneo plano (el directorio de salida no tiene subcarpetas).
    while (struct dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        const std::string full = dir_ + "/" + name;
        struct stat st{};
        if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (hasAudioExtension(name)) {
            paths_.push_back(full);
        }
    }
    ::closedir(dir);

    std::sort(paths_.begin(), paths_.end());

    core::Logger::instance().info(
        "found %zu playable file(s) in '%s'", paths_.size(), dir_.c_str());
    return true;
}

const std::string& TrackList::pathAt(std::size_t index) const {
    static const std::string kEmpty;
    if (index >= paths_.size()) return kEmpty;
    return paths_[index];
}

std::string TrackList::nameAt(std::size_t index) const {
    if (index >= paths_.size()) return std::string();

    // "output/recording_202608121137.mp3" -> "recording_202608121137.mp3"
    // (la extensión se conserva para distinguir WAV de MP3 en la lista).
    const std::string& full = paths_[index];
    const size_t slash = full.find_last_of('/');
    return full.substr(slash == std::string::npos ? 0 : slash + 1);
}

} // namespace SOUND_LIST
