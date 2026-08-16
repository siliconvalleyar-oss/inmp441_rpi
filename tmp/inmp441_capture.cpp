/*
 * inmp441_capture.cpp
 * ---------------------------------------------------------
 * Captura de audio desde el microfono digital INMP441 (I2S)
 * en Raspberry Pi, usando ALSA.
 *
 * Basado en las especificaciones del datasheet DS-INMP441-00 Rev 1.1:
 *   - Formato I2S, 24-bit, complemento a 2 (twos complement), MSB primero.
 *   - 64 ciclos de SCK por frame WS (32 ciclos por canal).
 *   - fSCK = 64 x fWS  (fWS = frecuencia de muestreo deseada).
 *   - El microfono deja su salida en HIGH-Z cuando no transmite datos
 *     (por eso ALSA lo ve como PCM S32_LE, con los 24 bits utiles
 *     alineados a la izquierda dentro de los 32 bits, MSB-first).
 *   - Rango de muestreo recomendado por WS: 7.8 kHz a 50 kHz -> se usa 48000 Hz.
 *
 * Requiere:
 *   sudo apt-get install libasound2-dev
 *   Habilitar overlay I2S en /boot/config.txt, por ejemplo:
 *     dtoverlay=googlevoicehat-soundcard
 *   (o el overlay especifico de tu HAT/adaptador I2S-MEMS)
 *
 * Compilar:
 *   g++ -O2 -o inmp441_capture inmp441_capture.cpp -lasound
 *
 * Ejecutar:
 *   ./inmp441_capture salida.wav 10        // graba 10 segundos
 *
 * Notas de conexionado (segun Tabla 6 del datasheet):
 *   SD   -> GPIO20 (I2S DIN)
 *   SCK  -> GPIO18 (I2S BCLK)
 *   WS   -> GPIO19 (I2S LRCLK)
 *   L/R  -> GND (canal izquierdo) o VDD (canal derecho)
 *   VDD  -> 3.3V (con capacitor de desacople 0.1uF entre VDD y GND, Pin 7)
 *   GND  -> GND
 *   CHIPEN -> VDD (habilitado) o dejar flotante segun diseno del HAT
 */

#include <alsa/asoundlib.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------
// Parametros derivados del datasheet (Tabla 3: Serial Data Port Timing)
// ---------------------------------------------------------------------
static const unsigned int SAMPLE_RATE_REQUESTED = 48000; // fWS solicitada,
                                                           // dentro de 7.8k-50kHz.
                                                           // La tasa REAL se lee
                                                           // del hardware tras
                                                           // configurar ALSA
                                                           // (ver punto 2 del
                                                           // analisis de errores).
static const unsigned int CHANNELS      = 2;      // El bus I2S siempre maneja
                                                    // frames stereo (L+R),
                                                    // aunque solo haya 1 mic.
static const unsigned int BITS_PER_WORD = 32;      // ALSA entrega 32 bits por
                                                    // slot; los 24 bits de
                                                    // audio del INMP441 estan
                                                    // alineados a MSB.
static const char* DEFAULT_PCM_DEVICE = "plughw:0,0"; // Valor por defecto;
                                                        // se puede sobreescribir
                                                        // por linea de comandos.
                                                        // Ejecutar `arecord -l`
                                                        // para ver el nombre
                                                        // real de la tarjeta
                                                        // I2S (p.ej. la tarjeta
                                                        // del overlay
                                                        // googlevoicehat-soundcard
                                                        // suele NO ser la 0 si
                                                        // hay HDMI/audio
                                                        // integrado presente).

// Cabecera minima de archivo WAV (PCM 16-bit, para reproduccion facil)
struct WavHeader {
    char riff[4] = {'R','I','F','F'};
    uint32_t chunkSize;
    char wave[4] = {'W','A','V','E'};
    char fmt[4]  = {'f','m','t',' '};
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char data[4] = {'d','a','t','a'};
    uint32_t dataSize;
};

// Extrae la muestra de 24 bits (canal seleccionado con L/R) desde
// una palabra de 32 bits recibida por ALSA en formato S32_LE, y la
// convierte a 16 bits con signo para escribirla en el WAV.
//
// CORRECCION (punto 1 del analisis): el dato de 24 bits del INMP441
// llega alineado al MSB (bit 31) dentro del slot I2S de 32 bits, segun
// "Data-Word Format" del datasheet. Para quedarnos con los 16 bits MAS
// significativos de esos 24 (y no los menos significativos, que es lo
// que hacia la version anterior), basta un unico shift aritmetico de
// 16 posiciones sobre el valor de 32 bits original:
//     bits 31..16  ->  16 bits mas significativos de la muestra de 24 bits
// El shift aritmetico (>>) sobre un int32_t con signo preserva el signo,
// lo cual es correcto porque el formato es complemento a 2.
static int16_t extract16FromI2S32(int32_t raw) {
    return static_cast<int16_t>(raw >> 16);
}

// Dithering TPDF opcional (punto 6 del analisis): reduce la distorsion
// armonica por truncamiento en senales de bajo nivel, sumando ruido
// triangular de muy baja amplitud antes de truncar a 16 bits. Deshabilitado
// por defecto (ENABLE_DITHER = false) para mantener el comportamiento
// simple; activarlo si se requiere calidad de audio mas precisa.
static const bool ENABLE_DITHER = false;

static int16_t extract16FromI2S32Dithered(int32_t raw) {
    if (!ENABLE_DITHER) {
        return extract16FromI2S32(raw);
    }
    // TPDF: suma de dos generadores uniformes independientes, escalados
    // a +/- 1 LSB del resultado de 16 bits (es decir, +/- (1<<15) en la
    // escala de 32 bits antes del shift final).
    int32_t noise = (rand() % 65536) - (rand() % 65536); // rango ~ +/-65535
    int32_t dithered = raw + noise;
    return static_cast<int16_t>(dithered >> 16);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr,
            "Uso: %s salida.wav duracion_segundos [dispositivo_pcm] [canal]\n"
            "  dispositivo_pcm: por defecto '%s'. Ejecute 'arecord -l' si falla\n"
            "                   la apertura, para ver el nombre real de su\n"
            "                   tarjeta I2S.\n"
            "  canal: 0 = izquierdo (L/R->GND, por defecto), 1 = derecho (L/R->VDD)\n",
            argv[0], DEFAULT_PCM_DEVICE);
        return 1;
    }
    std::string outFile = argv[1];
    int durationSec = atoi(argv[2]);
    std::string pcmDevice = (argc >= 4) ? argv[3] : DEFAULT_PCM_DEVICE;
    int channelIndex = (argc >= 5) ? atoi(argv[4]) : 0; // 0=izq, 1=der

    if (channelIndex != 0 && channelIndex != 1) {
        fprintf(stderr, "Canal invalido (%d). Debe ser 0 (izquierdo) o 1 (derecho).\n",
                channelIndex);
        return 1;
    }

    snd_pcm_t* pcmHandle;
    snd_pcm_hw_params_t* hwParams;
    int err;

    if ((err = snd_pcm_open(&pcmHandle, pcmDevice.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "No se pudo abrir dispositivo PCM '%s': %s\n"
                "Sugerencia: ejecute 'arecord -l' para listar las tarjetas\n"
                "disponibles y pase el nombre correcto como 3er argumento.\n",
                pcmDevice.c_str(), snd_strerror(err));
        return 1;
    }

    snd_pcm_hw_params_alloca(&hwParams);
    snd_pcm_hw_params_any(pcmHandle, hwParams);
    snd_pcm_hw_params_set_access(pcmHandle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcmHandle, hwParams, SND_PCM_FORMAT_S32_LE);
    snd_pcm_hw_params_set_channels(pcmHandle, hwParams, CHANNELS);

    unsigned int rate = SAMPLE_RATE_REQUESTED;
    snd_pcm_hw_params_set_rate_near(pcmHandle, hwParams, &rate, nullptr);

    snd_pcm_uframes_t frames = 1024;
    snd_pcm_hw_params_set_period_size_near(pcmHandle, hwParams, &frames, nullptr);

    if ((err = snd_pcm_hw_params(pcmHandle, hwParams)) < 0) {
        fprintf(stderr, "No se pudieron fijar los parametros HW: %s\n", snd_strerror(err));
        snd_pcm_close(pcmHandle);
        return 1;
    }

    // CORRECCION (punto 2 del analisis): leer la tasa REAL que el hardware
    // acepto, ya que set_rate_near puede redondear a un valor distinto del
    // solicitado. A partir de aqui se usa 'rate' (actualizada) para todos
    // los calculos de duracion y para la cabecera WAV, evitando
    // desincronizacion entre el tiempo real grabado y el archivo generado.
    int dir = 0;
    if ((err = snd_pcm_hw_params_get_rate(hwParams, &rate, &dir)) < 0) {
        fprintf(stderr, "No se pudo leer la tasa de muestreo real: %s\n", snd_strerror(err));
        snd_pcm_close(pcmHandle);
        return 1;
    }
    if (rate != SAMPLE_RATE_REQUESTED) {
        fprintf(stderr, "Aviso: tasa solicitada %u Hz, tasa real asignada por el hardware: %u Hz\n",
                SAMPLE_RATE_REQUESTED, rate);
    }

    snd_pcm_hw_params_get_period_size(hwParams, &frames, 0);

    size_t bufBytes = frames * CHANNELS * (BITS_PER_WORD / 8);
    std::vector<int32_t> buffer(bufBytes / sizeof(int32_t));

    unsigned int totalFramesNeeded = rate * durationSec; // usa tasa REAL
    unsigned int framesCaptured = 0;
    unsigned int overrunCount = 0;

    std::vector<int16_t> pcmOut; // un solo canal (mono), segun channelIndex
    pcmOut.reserve(totalFramesNeeded);

    printf("Grabando %d segundos a %u Hz desde %s (canal %s)...\n",
           durationSec, rate, pcmDevice.c_str(),
           channelIndex == 0 ? "izquierdo" : "derecho");

    while (framesCaptured < totalFramesNeeded) {
        int readFrames = snd_pcm_readi(pcmHandle, buffer.data(), frames);
        if (readFrames == -EPIPE) {
            // CORRECCION (punto 7 del analisis): un overrun implica
            // muestras perdidas de forma irrecuperable. Se informa
            // explicitamente para que el usuario sepa que la duracion
            // final del archivo puede ser menor a la solicitada si esto
            // ocurre repetidamente.
            overrunCount++;
            fprintf(stderr, "Overrun detectado (#%u), recuperando...\n", overrunCount);
            snd_pcm_prepare(pcmHandle);
            continue;
        } else if (readFrames < 0) {
            fprintf(stderr, "Error de lectura: %s\n", snd_strerror(readFrames));
            break;
        }

        for (int i = 0; i < readFrames; i++) {
            int32_t sample = buffer[i * CHANNELS + channelIndex];
            pcmOut.push_back(extract16FromI2S32Dithered(sample));
        }
        framesCaptured += readFrames;
    }

    if (overrunCount > 0) {
        fprintf(stderr,
            "Aviso: se detectaron %u overrun(s) durante la grabacion. "
            "La duracion efectiva puede ser menor a la solicitada debido a "
            "muestras perdidas.\n", overrunCount);
    }

    snd_pcm_close(pcmHandle);

    // Escribir archivo WAV mono, 16-bit
    WavHeader header;
    header.numChannels = 1;
    header.sampleRate = rate;
    header.bitsPerSample = 16;
    header.blockAlign = header.numChannels * header.bitsPerSample / 8;
    header.byteRate = header.sampleRate * header.blockAlign;
    header.dataSize = static_cast<uint32_t>(pcmOut.size() * sizeof(int16_t));
    header.chunkSize = 36 + header.dataSize;

    std::ofstream ofs(outFile, std::ios::binary);
    if (!ofs) {
        fprintf(stderr, "No se pudo crear archivo de salida '%s'\n", outFile.c_str());
        return 1;
    }
    ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
    ofs.write(reinterpret_cast<const char*>(pcmOut.data()), header.dataSize);
    ofs.close();

    printf("Grabacion finalizada: %s (%zu muestras)\n", outFile.c_str(), pcmOut.size());
    return 0;
}
