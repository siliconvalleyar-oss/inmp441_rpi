// Grabador y reproductor de audio para micrófono I2S INMP441 en Raspberry
// Pi. Compatible con Raspberry Pi 4 (64-bit) y Raspberry Pi Zero 2W (32-bit).
//
// Pinout (bus I2S por hardware del SoC BCM):
//   GPIO18  BCLK  -> INMP441 SCK
//   GPIO19  WS    -> INMP441 WS
//   GPIO20  SD    <- INMP441 SD
//   GPIO21  L/R   -> selección de canal (estático, no es parte del I2S)
//
// El audio en sí (BCLK/WS/SD) lo maneja el periférico I2S de hardware del
// SoC vía ALSA/kernel. GPIO21 se maneja directo por libgpiod porque el
// INMP441 sólo lo lee como nivel estático para elegir el slot L o R.
//
// Todas las grabaciones se guardan en output/*.wav. El programa se puede
// usar por línea de comandos (para scripts/systemd) o mediante el menú
// interactivo (sin argumentos) para grabar y reproducir.

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "audio/alsa_capture.hpp"
#include "audio/alsa_device_finder.hpp"
#include "audio/alsa_playback.hpp"
#include "audio/wav_reader.hpp"
#include "audio/wav_writer.hpp"
#include "core/file_utils.hpp"
#include "gpio/gpio_channel_select.hpp"

namespace {

constexpr const char* kOutputDir = "output";

std::atomic<bool> g_stop_requested{false};

void OnSignal(int) { g_stop_requested = true; }

struct AppOptions {
    std::string output_path;  // vacío = generar nombre automático
    std::string alsa_device = "default";
    uint32_t sample_rate = 48000;
    uint16_t bits_per_sample = 32;
    unsigned int lr_gpio = 21;
    std::string gpio_chip = "gpiochip0";
    i2c_audio::MicChannel channel = i2c_audio::MicChannel::Right;
    int duration_seconds = 0;  // 0 = grabar hasta Ctrl+C

    bool dither = false;  // dithering TPDF al convertir a 16 bits

    bool menu_mode = false;
    bool list_mode = false;
    std::string play_file;  // no vacío = reproducir y salir
};

void PrintUsage(const char* argv0) {
    std::cout
        << "Uso: " << argv0 << " [opciones]\n"
        << "Sin opciones: abre el menú interactivo.\n\n"
        << "Grabación:\n"
        << "  -o <archivo>       Nombre del WAV de salida dentro de output/\n"
        << "                     (default: grabacion_YYYYMMDD_HHMMSS.wav)\n"
        << "  -d <dispositivo>   Dispositivo ALSA (default: \"default\", ej: hw:0,0)\n"
        << "  -r <hz>            Sample rate (default: 48000)\n"
        << "  -b <bits>          Bits por muestra: 16|24|32 (default: 32)\n"
        << "  -c <L|R>           Canal del INMP441 (default: R)\n"
        << "  -g <gpio>          GPIO BCM para L/R select (default: 21)\n"
        << "  -t <segundos>      Duración; 0 = hasta Ctrl+C (default: 0)\n"
        << "  --dither           Dithering TPDF al convertir a 16 bits\n"
        << "Reproducción:\n"
        << "  -l                 Listar grabaciones en output/\n"
        << "  -p <archivo>       Reproducir output/<archivo> y salir\n"
        << "Otros:\n"
        << "  -m                 Forzar menú interactivo\n"
        << "  -h                 Ayuda\n";
}

bool ParseArgs(int argc, char** argv, AppOptions* opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Falta valor para " << flag << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-o") {
            if (auto v = next("-o")) opts->output_path = v; else return false;
        } else if (arg == "-d") {
            if (auto v = next("-d")) opts->alsa_device = v; else return false;
        } else if (arg == "-r") {
            if (auto v = next("-r")) opts->sample_rate = std::stoul(v); else return false;
        } else if (arg == "-b") {
            if (auto v = next("-b")) opts->bits_per_sample = static_cast<uint16_t>(std::stoul(v)); else return false;
        } else if (arg == "-c") {
            if (auto v = next("-c")) {
                opts->channel = (std::string(v) == "R" || std::string(v) == "r")
                                      ? i2c_audio::MicChannel::Right
                                      : i2c_audio::MicChannel::Left;
            } else return false;
        } else if (arg == "-g") {
            if (auto v = next("-g")) opts->lr_gpio = static_cast<unsigned int>(std::stoul(v)); else return false;
        } else if (arg == "-t") {
            if (auto v = next("-t")) opts->duration_seconds = std::stoi(v); else return false;
        } else if (arg == "--dither") {
            opts->dither = true;
        } else if (arg == "-l") {
            opts->list_mode = true;
        } else if (arg == "-p") {
            if (auto v = next("-p")) opts->play_file = v; else return false;
        } else if (arg == "-m") {
            opts->menu_mode = true;
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Opción desconocida: " << arg << "\n";
            return false;
        }
    }
    return true;
}

// Si el usuario no especificó -d explícitamente (o sea, sigue en
// "default"), intenta encontrar la tarjeta del INMP441 por nombre en vez
// de depender de un dispositivo ALSA "default" que en Raspberry Pi OS no
// siempre está definido para overlays custom como inmp441-bare.
std::string ResolveAlsaDevice(const std::string& requested_device) {
    if (requested_device != "default") return requested_device;

    if (auto found = i2c_audio::FindAlsaDeviceByName("bare")) {
        std::cout << "Dispositivo ALSA auto-detectado: " << *found << "\n";
        return *found;
    }

    std::cerr << "Aviso: no se encontró automáticamente la tarjeta I2S "
                "(buscando 'bare' en 'arecord -l'). Se intentará con "
                "'default', que puede fallar. Si falla, corré con "
                "-d plughw:<card>,0 usando el número que te muestre "
                "'arecord -l'.\n";
    return requested_device;
}

std::string GenerateTimestampedName() {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    char buf[64];
    std::strftime(buf, sizeof(buf), "grabacion_%Y%m%d_%H%M%S.wav", &tm_buf);
    return std::string(buf);
}

// Graba a output/<nombre>.wav según las opciones dadas. Devuelve el path
// completo del archivo generado, o "" si falló.
std::string RunRecording(const AppOptions& opts) {
    if (!i2c_audio::EnsureDirectoryExists(kOutputDir)) {
        std::cerr << "No se pudo crear/acceder a la carpeta '" << kOutputDir
                   << "'\n";
        return "";
    }

    const std::string filename =
        opts.output_path.empty() ? GenerateTimestampedName() : opts.output_path;
    const std::string full_path = std::string(kOutputDir) + "/" + filename;

    i2c_audio::GpioChannelSelect channel_select(opts.lr_gpio, opts.gpio_chip);
    if (!channel_select.Init() || !channel_select.SetChannel(opts.channel)) {
        std::cerr << "No se pudo configurar el GPIO de L/R select "
                   << "(¿corriste con permisos suficientes / grupo gpio?).\n";
        return "";
    }
    std::cout << "Canal seleccionado: "
               << (opts.channel == i2c_audio::MicChannel::Left ? "Izquierdo (L)"
                                                                    : "Derecho (R)")
               << " (GPIO" << opts.lr_gpio << ")\n";

    i2c_audio::CaptureConfig cap_cfg;
    cap_cfg.device_name = ResolveAlsaDevice(opts.alsa_device);
    cap_cfg.sample_rate = opts.sample_rate;
    // La captura siempre es S32_LE (el INMP441 entrega 24 bits útiles dentro
    // de slots de 32 bits, alineados a MSB); la conversión al ancho de salida
    // pedido con -b se hace abajo en el bucle.
    cap_cfg.bits_per_sample = 32;
    // La tarjeta I2S entrega 2 slots (TDM). El INMP441 coloca su muestra en
    // un solo slot (izquierdo o derecho según el pin L/R); el otro slot es
    // silencio. Se capturan los 2 y abajo se conserva solo el slot del canal
    // seleccionado, escribiendo un WAV mono.
    cap_cfg.num_channels = 2;

    i2c_audio::AlsaCapture capture(cap_cfg);
    if (!capture.Open()) {
        std::cerr << "No se pudo abrir el dispositivo ALSA '"
                   << opts.alsa_device << "'.\n"
                   << "Revisá que el overlay I2S esté activo y listá "
                   << "tarjetas con 'arecord -l'.\n";
        return "";
    }

    i2c_audio::WavWriter wav(full_path, capture.config().sample_rate,
                                opts.bits_per_sample,
                                /*num_channels=*/1);
    if (!wav.IsOpen()) {
        std::cerr << "No se pudo crear el archivo de salida '" << full_path
                   << "'.\n";
        return "";
    }

    std::cout << "Grabando en '" << full_path << "' @ "
               << capture.config().sample_rate << " Hz, "
               << opts.bits_per_sample << " bits. "
               << (opts.duration_seconds > 0
                       ? "Duración: " + std::to_string(opts.duration_seconds) + "s"
                       : "Presioná Ctrl+C para detener.")
               << "\n";

    const unsigned int slot = static_cast<unsigned int>(opts.channel);  // L=0, R=1
    const uint16_t out_sample_bytes = opts.bits_per_sample / 8;
    // Los 24 bits útiles del INMP441 llegan alineados a MSB en cada slot de
    // 32 bits; el byte bajo es relleno y se descarta. Para salida de 16 bits
    // se toman los 16 bits más significativos; para 24/32 bits se conservan
    // los 24 útiles (shift = 8) y quedan alineados a LSB.
    const int shift = (opts.bits_per_sample == 16) ? 16 : 8;
    const double bytes_per_second =
        static_cast<double>(capture.config().sample_rate) * out_sample_bytes;
    const double bytes_target = opts.duration_seconds > 0
                                       ? bytes_per_second * opts.duration_seconds
                                       : -1.0;
    double bytes_written = 0.0;

    std::vector<uint8_t> mono_buffer(capture.config().period_frames *
                                     out_sample_bytes);

    g_stop_requested = false;
    capture.CaptureLoop([&](const uint8_t* data, size_t bytes) -> bool {
        const size_t frame_bytes = 2 * sizeof(int32_t);
        const size_t frames = bytes / frame_bytes;
        const size_t mono_bytes = frames * out_sample_bytes;
        for (size_t f = 0; f < frames; ++f) {
            const size_t src = f * frame_bytes + slot * sizeof(int32_t);
            int32_t raw = 0;
            std::memcpy(&raw, data + src, sizeof(raw));
            // Dithering TPDF opcional: suma ruido triangular de ~1 LSB (en
            // la escala de 16 bits) antes de truncar, para reducir la
            // distorsión armónica en señales de bajo nivel. Solo aplica
            // cuando se convierte a 16 bits.
            if (opts.dither && shift == 16) {
                raw += (std::rand() % 32768) - (std::rand() % 32768);
            }
            const int32_t value = raw >> shift;  // desplazamiento con signo
            std::memcpy(mono_buffer.data() + f * out_sample_bytes, &value,
                        out_sample_bytes);
        }
        if (!wav.Write(mono_buffer.data(), mono_bytes)) {
            std::cerr << "Error escribiendo al WAV, deteniendo.\n";
            return false;
        }
        bytes_written += static_cast<double>(mono_bytes);
        if (g_stop_requested) return false;
        if (bytes_target > 0 && bytes_written >= bytes_target) return false;
        return true;
    });

    wav.Finalize();
    capture.Close();

    if (capture.xrun_count() > 0) {
        std::cerr << "Aviso: se detectaron " << capture.xrun_count()
                   << " overrun(s). La duración efectiva del archivo puede "
                      "ser menor a la solicitada.\n";
    }

    std::cout << "Grabación finalizada: " << full_path << "\n";
    return full_path;
}

// Reproduce output/<filename>. Devuelve false si falló.
bool PlayFile(const std::string& filename, const std::string& device) {
    const std::string full_path = std::string(kOutputDir) + "/" + filename;

    i2c_audio::WavReader wav(full_path);
    if (!wav.IsValid()) {
        std::cerr << "No se pudo leer '" << full_path << "' como WAV válido.\n";
        return false;
    }

    const std::string resolved_device = ResolveAlsaDevice(device);
    i2c_audio::AlsaPlayback playback(resolved_device);
    if (!playback.Open(wav)) {
        std::cerr << "No se pudo abrir el dispositivo de reproducción '"
                   << resolved_device << "'.\n";
        return false;
    }

    std::cout << "Reproduciendo '" << full_path << "' (" << wav.sample_rate()
               << " Hz, " << wav.bits_per_sample() << " bits, "
               << wav.num_channels() << " canal/es)...\n";
    const bool ok = playback.PlayAll(wav);
    if (ok) std::cout << "Reproducción finalizada.\n";
    return ok;
}

void PrintWavList(const std::vector<std::string>& files) {
    if (files.empty()) {
        std::cout << "(no hay grabaciones en '" << kOutputDir << "')\n";
        return;
    }
    for (size_t i = 0; i < files.size(); ++i) {
        std::cout << "  " << (i + 1) << ") " << files[i] << "\n";
    }
}

// Menú interactivo por consola: grabar o reproducir archivos de output/.
void RunMenu(AppOptions opts) {
    while (true) {
        std::cout << "\n=== Grabador INMP441 ===\n"
                   << "Dispositivo ALSA actual: " << opts.alsa_device
                   << (opts.alsa_device == "default" ? " (se auto-detecta al usarlo)" : "")
                   << "\n"
                   << "1) Grabar nuevo audio\n"
                   << "2) Reproducir una grabación\n"
                   << "3) Listar grabaciones\n"
                   << "4) Cambiar dispositivo ALSA\n"
                   << "5) Salir\n"
                   << "Elegí una opción: ";

        std::string choice;
        if (!std::getline(std::cin, choice)) break;

        if (choice == "1") {
            std::cout << "Duración en segundos (0 = hasta Ctrl+C): ";
            std::string dur_str;
            std::getline(std::cin, dur_str);
            try {
                opts.duration_seconds = dur_str.empty() ? 0 : std::stoi(dur_str);
            } catch (...) {
                opts.duration_seconds = 0;
            }
            opts.output_path.clear();  // nombre automático con timestamp
            RunRecording(opts);

        } else if (choice == "2") {
            auto files = i2c_audio::ListWavFiles(kOutputDir);
            PrintWavList(files);
            if (files.empty()) continue;

            std::cout << "Número a reproducir (0 = cancelar): ";
            std::string sel_str;
            std::getline(std::cin, sel_str);
            int sel = 0;
            try {
                sel = std::stoi(sel_str);
            } catch (...) {
                continue;
            }
            if (sel <= 0 || static_cast<size_t>(sel) > files.size()) continue;
            PlayFile(files[static_cast<size_t>(sel) - 1], opts.alsa_device);

        } else if (choice == "3") {
            PrintWavList(i2c_audio::ListWavFiles(kOutputDir));

        } else if (choice == "4") {
            std::cout << "Dispositivo ALSA (ej. plughw:3,0, o 'default' "
                        "para auto-detectar): ";
            std::string dev;
            std::getline(std::cin, dev);
            if (!dev.empty()) opts.alsa_device = dev;

        } else if (choice == "5") {
            break;
        } else {
            std::cout << "Opción inválida.\n";
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    AppOptions opts;
    if (!ParseArgs(argc, argv, &opts)) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    i2c_audio::EnsureDirectoryExists(kOutputDir);

    if (opts.list_mode) {
        PrintWavList(i2c_audio::ListWavFiles(kOutputDir));
        return 0;
    }

    if (!opts.play_file.empty()) {
        return PlayFile(opts.play_file, opts.alsa_device) ? 0 : 1;
    }

    // Si se ejecuta sin ningún argumento, abrimos el menú por comodidad.
    const bool no_args = (argc == 1);
    if (opts.menu_mode || no_args) {
        RunMenu(opts);
        return 0;
    }

    // Modo grabación directa por línea de comandos (scripts, systemd, etc.)
    return RunRecording(opts).empty() ? 1 : 0;
}
