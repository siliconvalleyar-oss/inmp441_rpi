//////////////////////////////////////////////////////////////////
//
//                  player.cpp
//
// Descripción: Implementación del motor de reproducción MP3/WAV.
//
//              Decodificación : libmpg123 (MPEG audio y PCM WAV)
//              Salida de audio: libao (driver por defecto del
//                               sistema; con prioridad pulse, que es
//                               donde vive el sink Bluetooth A2DP)
//
//              La reproducción corre en un hilo propio. La pausa se
//              implementa simplemente dejando de leer datos del
//              decodificador (la posición no se pierde). La parada
//              se implementa con una bandera atómica que el bucle
//              comprueba en cada iteración.
//
// Autor: lion
// Fecha: 2024 Octb (refactorizado)
//
//////////////////////////////////////////////////////////////////

#include "player.hpp"

#include <ao/ao.h>
#include <mpg123.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace PLAYER {

namespace {

// Los mensajes del hilo de reproducción van a un archivo de log, no a
// stderr: stderr se intercala con el refresco del menú y lo corrompe.
FILE* openLog() {
    static FILE* f = std::fopen("/tmp/mp3_player.log", "a");
    return f;
}

// True si los primeros 12 bytes del archivo son la cabecera RIFF/WAVE.
// libmpg123 (la librería) solo decodifica MPEG, así que los WAV PCM se
// reproducen con un lector propio (playWav).
bool isRiffWave(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    unsigned char hdr[12];
    const std::size_t n = std::fread(hdr, 1, sizeof(hdr), f);
    std::fclose(f);
    if (n != sizeof(hdr)) return false;
    return hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F' &&
           hdr[8] == 'W' && hdr[9] == 'A' && hdr[10] == 'V' && hdr[11] == 'E';
}

void playerLog(const char* fmt, ...) {
    FILE* f = openLog();
    if (f == nullptr) return;
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args);
    va_end(args);
    std::fflush(f);
}

// Silencia temporalmente stderr (para que ALSA no llene la terminal de
// errores de configuración al probar dispositivos de salida).
class StderrSilencer {
public:
    StderrSilencer() {
        saved_ = dup(STDERR_FILENO);
        const int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, STDERR_FILENO);
            close(nullfd);
        }
    }
    ~StderrSilencer() {
        dup2(saved_, STDERR_FILENO);
        close(saved_);
    }
private:
    int saved_;
};

// Abre el dispositivo de salida probando varios drivers de libao.
// Prioridad: PulseAudio (sink Bluetooth) > ALSA > driver por defecto.
ao_device* openAudioDevice(ao_sample_format& fmt) {
    const char* candidates[] = {"pulse", "alsa", nullptr};
    for (int i = 0; candidates[i] != nullptr; ++i) {
        const int id = ao_driver_id(candidates[i]);
        if (id < 0) continue;
        StderrSilencer silent; // ALSA es muy ruidoso al fallar
        ao_device* dev = ao_open_live(id, &fmt, nullptr);
        if (dev != nullptr) {
            playerLog("[PLAYER] Output: driver '%s'.\n", candidates[i]);
            return dev;
        }
    }
    StderrSilencer silent;
    return ao_open_live(ao_default_driver_id(), &fmt, nullptr);
}

} // namespace

Player::Player() {
    ao_initialize();
}

Player::~Player() {
    stop();
    ao_shutdown();
}

void Player::play(const std::string& path) {
    stop();

    {
        std::lock_guard<std::mutex> lock(fileMutex_);
        file_ = path;
    }

    stopRequested_ = false;
    paused_ = false;
    finished_ = false;
    playing_ = true;
    durationSeconds_ = 0.0;
    positionSeconds_ = 0.0;
    thread_ = std::thread(&Player::playbackLoop, this);
}

void Player::stop() {
    stopRequested_ = true;
    paused_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    playing_ = false;
    finished_ = false;  // una parada deliberada no es un fin natural de pista
    durationSeconds_ = 0.0;
    positionSeconds_ = 0.0;
}

void Player::togglePause() {
    if (!playing_.load()) return;
    paused_ = !paused_.load();
}

State Player::state() const {
    if (finished_.load() && !playing_.load()) return State::Finished;
    if (!playing_.load()) return State::Stopped;
    return paused_.load() ? State::Paused : State::Playing;
}

std::string Player::currentFile() const {
    std::lock_guard<std::mutex> lock(fileMutex_);
    return file_;
}

void Player::playbackLoop() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(fileMutex_);
        path = file_;
    }

    // WAV (PCM) se decodifica en proceso; mpg123 queda para los MP3.
    if (isRiffWave(path)) {
        playWav(path);
        return;
    }

    mpg123_init();

    int err = 0;
    mpg123_handle* mh = mpg123_new(nullptr, &err);
    if (mh == nullptr) {
        playerLog("[PLAYER] cannot create mpg123: %s\n",
                  mpg123_plain_strerror(err));
        playing_ = false;
        return;
    }

    err = mpg123_open(mh, path.c_str());
    if (err != MPG123_OK) {
        playerLog("[PLAYER] cannot open '%s': %s\n",
                  path.c_str(), mpg123_strerror(mh));
        mpg123_delete(mh);
        mpg123_exit();
        playing_ = false;
        return;
    }

    // Forzar salida PCM de 16 bits con el formato nativo del archivo.
    long rate = 0;
    int channels = 0, encoding = 0;
    mpg123_getformat(mh, &rate, &channels, &encoding);
    mpg123_format_none(mh);
    mpg123_format(mh, rate, channels, MPG123_ENC_SIGNED_16);

    // Duración total: mpg123_length está en muestras por canal. Puede
    // devolver <= 0 en streams/duraciones desconocidas: en ese caso la
    // barra de progreso se dibuja sin total (--:--).
    const long totalSamples = mpg123_length(mh);
    if (totalSamples > 0 && rate > 0) {
        durationSeconds_ = static_cast<double>(totalSamples) / static_cast<double>(rate);
    }

    ao_sample_format fmt{};
    fmt.bits = 16;
    fmt.rate = static_cast<int>(rate);
    fmt.channels = channels;
    fmt.byte_format = AO_FMT_NATIVE;

    // Probar primero PulseAudio (donde vive el sink Bluetooth), luego
    // ALSA y por último el driver por defecto del sistema.
    ao_device* dev = openAudioDevice(fmt);
    if (dev == nullptr) {
        playerLog(
            "[PLAYER] no audio output device. "
            "Connect the Bluetooth speaker (A2DP) and make sure "
            "PulseAudio is running. Track: '%s'\n",
            path.c_str());
        mpg123_close(mh);
        mpg123_delete(mh);
        mpg123_exit();
        playing_ = false;
        return;
    }

    std::size_t outblock = mpg123_outblock(mh);
    if (outblock == 0) outblock = 4096;
    std::vector<unsigned char> buf(outblock);
    std::size_t done = 0;

    while (!stopRequested_.load()) {
        // Mientras esté en pausa, no consumimos datos del decodificador.
        while (paused_.load() && !stopRequested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (stopRequested_.load()) break;

        const int rc = mpg123_read(mh, buf.data(), buf.size(), &done);
        if (rc == MPG123_DONE) {
            break; // Fin de la pista
        }
        if (rc == MPG123_NEW_FORMAT) {
            continue; // Cambio de formato infrecuente: se descarta
        }
        if (rc != MPG123_OK) {
            playerLog("[PLAYER] decode error: %s\n",
                      mpg123_strerror(mh));
            break;
        }

        if (done > 0) {
            ao_play(dev, reinterpret_cast<char*>(buf.data()), static_cast<int>(done));
            // Posición actual (mpg123_tell está en muestras por canal).
            const long pos = mpg123_tell(mh);
            if (pos > 0 && rate > 0) {
                positionSeconds_ = static_cast<double>(pos) / static_cast<double>(rate);
            }
        }
    }

    ao_close(dev);
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();

    playing_ = false;
    paused_ = false;
    finished_ = true;
    if (durationSeconds_ > 0.0) positionSeconds_ = durationSeconds_.load();
}

// Reproduce un WAV PCM de 16 bits (mono o estéreo) directamente: lee la
// cabecera RIFF, abre el dispositivo de salida y vuelca el chunk `data`
// respetando pausa/parada igual que el bucle de MP3.
void Player::playWav(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        playerLog("[PLAYER] cannot open '%s'\n", path.c_str());
        playing_ = false;
        return;
    }

    // Saltar la cabecera RIFF de 12 bytes ("RIFF" + size + "WAVE").
    if (std::fseek(f, 12, SEEK_SET) != 0) {
        std::fclose(f);
        playing_ = false;
        return;
    }

    long dataSize = -1;
    int rate = 0, channels = 1, bits = 16;
    unsigned char b[8];

    // Recorrer los chunks: id(4) + size(4) + payload(size).
    while (std::fread(b, 1, 8, f) == 8) {
        const std::uint32_t size =
            static_cast<std::uint32_t>(b[4]) |
            (static_cast<std::uint32_t>(b[5]) << 8) |
            (static_cast<std::uint32_t>(b[6]) << 16) |
            (static_cast<std::uint32_t>(b[7]) << 24);

        if (b[0] == 'f' && b[1] == 'm' && b[2] == 't' && b[3] == ' ') {
            unsigned char fmt[16];
            if (size < 16 || std::fread(fmt, 1, 16, f) != 16) break;
            const std::uint16_t format =
                static_cast<std::uint16_t>(fmt[0] | (fmt[1] << 8));
            channels = fmt[2] | (fmt[3] << 8);
            rate = static_cast<int>(
                static_cast<std::uint32_t>(fmt[4]) |
                (static_cast<std::uint32_t>(fmt[5]) << 8) |
                (static_cast<std::uint32_t>(fmt[6]) << 16) |
                (static_cast<std::uint32_t>(fmt[7]) << 24));
            bits = fmt[14] | (fmt[15] << 8);
            if (format != 1 || bits != 16) {
                playerLog("[PLAYER] unsupported WAV (PCM format %u, %u-bit); "
                          "convert to MP3 or 16-bit PCM WAV\n", format, bits);
                std::fclose(f);
                playing_ = false;
                return;
            }
            if (size > 16) {
                std::fseek(f, static_cast<long>(size) - 16, SEEK_CUR);
            }
        } else if (b[0] == 'd' && b[1] == 'a' && b[2] == 't' && b[3] == 'a') {
            dataSize = static_cast<long>(size);
            break;
        } else {
            std::fseek(f, static_cast<long>(size), SEEK_CUR);
        }
    }

    if (dataSize < 0 || rate <= 0 || channels < 1 || channels > 2) {
        playerLog("[PLAYER] WAV '%s': fmt/data chunk missing or invalid\n",
                  path.c_str());
        std::fclose(f);
        playing_ = false;
        return;
    }

    // Duración total del WAV: dataSize / (rate * canales * 2 bytes).
    const double bytesPerSec = static_cast<double>(rate) * channels * 2.0;
    if (bytesPerSec > 0.0) {
        durationSeconds_ = static_cast<double>(dataSize) / bytesPerSec;
    }

    ao_sample_format fmt{};
    fmt.bits = 16;
    fmt.rate = rate;
    fmt.channels = channels;
    fmt.byte_format = AO_FMT_NATIVE;

    ao_device* dev = openAudioDevice(fmt);
    if (dev == nullptr) {
        playerLog(
            "[PLAYER] no audio output device. Connect the Bluetooth speaker "
            "(A2DP) and make sure PulseAudio is running. Track: '%s'\n",
            path.c_str());
        std::fclose(f);
        playing_ = false;
        return;
    }

    std::vector<unsigned char> buf(8192);
    long bytesPlayed = 0;
    while (!stopRequested_.load()) {
        // Mientras esté en pausa, no leemos más datos.
        while (paused_.load() && !stopRequested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (stopRequested_.load()) break;

        const std::size_t want =
            std::min(buf.size(), static_cast<std::size_t>(dataSize));
        if (want == 0) break;  // Fin del archivo

        const std::size_t n = std::fread(buf.data(), 1, want, f);
        if (n == 0) break;
        dataSize -= static_cast<long>(n);
        bytesPlayed += static_cast<long>(n);
        if (bytesPerSec > 0.0) {
            positionSeconds_ = static_cast<double>(bytesPlayed) / bytesPerSec;
        }
        ao_play(dev, reinterpret_cast<char*>(buf.data()), static_cast<int>(n));
    }

    ao_close(dev);
    std::fclose(f);
    playing_ = false;
    paused_ = false;
    finished_ = true;
    if (durationSeconds_ > 0.0) positionSeconds_ = durationSeconds_.load();
}

} // namespace PLAYER
