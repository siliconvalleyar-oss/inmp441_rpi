//////////////////////////////////////////////////////////////////
//
//                  track_list.hpp
//
// Descripción: Define la clase `TrackList`, la lista de archivos
//              reproducibles (WAV y MP3) del proyecto. Escanea el
//              directorio de salida (output/ por defecto, donde el
//              propio grabador deja sus grabaciones), ordena los
//              archivos alfabéticamente y expone métodos de acceso
//              a la ruta completa y al nombre mostrable.
//
// Autor: lion
// Fecha: 2024 Octb (refactorizado)
//
//////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace SOUND_LIST {

// Lista de pistas WAV/MP3 cargadas desde un directorio.
class TrackList {
public:
    // Constructor. `directory` es la carpeta donde se buscan las pistas.
    explicit TrackList(const std::string& directory = kDefaultDir);

    // Escanea el directorio y rellena la lista de pistas.
    // Devuelve true si todo fue bien (aunque la lista quede vacía).
    bool load();

    bool empty() const { return paths_.empty(); }
    std::size_t size() const { return paths_.size(); }

    // Ruta completa (directorio + nombre + extensión) de la pista `index`.
    const std::string& pathAt(std::size_t index) const;

    // Nombre mostrable de la pista `index` (sin directorio, con extensión
    // .wav/.mp3 para distinguir el formato de cada grabación).
    std::string nameAt(std::size_t index) const;

    const std::vector<std::string>& paths() const { return paths_; }
    const std::string& directory() const { return dir_; }

    // Directorio por defecto: la carpeta de salida de las grabaciones.
    static constexpr const char* kDefaultDir = "output";

private:
    std::string dir_;
    std::vector<std::string> paths_;
};

} // namespace SOUND_LIST
