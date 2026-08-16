#pragma once

#include <string>
#include <vector>

namespace i2c_audio {

// Devuelve la lista de archivos *.wav dentro de directory, ordenados
// alfabéticamente (nombre de archivo, sin path).
std::vector<std::string> ListWavFiles(const std::string& directory);

// Crea el directorio si no existe (equivalente a mkdir -p de un solo nivel).
bool EnsureDirectoryExists(const std::string& directory);

}  // namespace i2c_audio
