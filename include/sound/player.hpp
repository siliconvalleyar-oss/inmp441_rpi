//////////////////////////////////////////////////////////////////
//
//                  player.hpp
//
// Descripción: Define la clase `Player`, el motor de reproducción
//              MP3/WAV. Usa `libmpg123` para decodificar (mpg123
//              decodifica MP3 y también PCM WAV) y `libao` para la
//              salida de audio. El audio se reproduce en un hilo
//              aparte para que el menú siga respondiendo mientras
//              suena la pista.
//
//              La salida de audio de `libao` va al dispositivo por
//              defecto del sistema; cuando un altavoz Bluetooth está
//              conectado como A2DP, ese será el dispositivo de salida.
//
// Autor: lion
// Fecha: 2024 Octb (refactorizado)
//
//////////////////////////////////////////////////////////////////

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace PLAYER {

enum class State {
    Stopped,   // No hay nada reproduciéndose
    Playing,   // Reproduciendo
    Paused,    // En pausa
    Finished   // La pista terminó de forma natural
};

// Motor de reproducción asíncrono (MP3 y WAV).
class Player {
public:
    Player();
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Inicia la reproducción de `path` (detiene la pista anterior).
    void play(const std::string& path);

    // Detiene la reproducción actual.
    void stop();

    // Alterna pausa/reanudación.
    void togglePause();

    bool isPlaying() const { return playing_.load(); }
    bool isPaused() const { return paused_.load(); }

    State state() const;

    // Ruta de la pista en reproducción (o la última pedida).
    std::string currentFile() const;

    // Duración total de la pista en segundos (0 si se desconoce o no hay
    // pista). Se usa para dibujar la barra de progreso y el tiempo.
    double duration() const { return durationSeconds_.load(); }

    // Posición actual de la pista en segundos (0 si no hay pista).
    double position() const { return positionSeconds_.load(); }

private:
    void playbackLoop();
    void playWav(const std::string& path);  // WAV PCM 16-bit in-process

    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> finished_{false};
    std::atomic<double> durationSeconds_{0.0};
    std::atomic<double> positionSeconds_{0.0};

    mutable std::mutex fileMutex_;
    std::string file_;
};

} // namespace PLAYER
