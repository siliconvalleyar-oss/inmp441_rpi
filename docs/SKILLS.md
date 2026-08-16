# SKILLS — Conocimiento del Proyecto inmp441_rpi

## 1. Visión general

`inmp441_rpi` lee audio de un micrófono **TDK InvenSense INMP441** (MEMS I2S, 24-bit)
a través de **ALSA**, con el driver I2S del kernel expuesto por el overlay
`dtoverlay=inmp441-bare`. La línea L/R select (GPIO21) se controla con **libgpiod**
(sin root). `bcm2835` se sigue usando, pero solo para el display **OLED** (SSD1306).

- **Lenguaje:** C++17
- **Target:** Raspberry Pi Zero 2 W (BCM2835/BCM2710), funciona en 32-bit y 64-bit
- **Build system:** Makefile plano, sin cmake
- **Dependencias externas:** libasound2-dev + libgpiod-dev (pkg-config), bcm2835 (OLED), libmpg123, libao, lame
- **Licencia:** MIT

## 2. Hardware y wiring

| INMP441 pin | GPIO Pi | Función | Notas |
|---|---|---|---|
| VDD | 3.3 V | Alimentación | |
| GND | GND | Tierra | |
| SD (PCM_DIN) | GPIO 20 | Datos I2S | |
| SCK / BCLK | GPIO 18 | Bit clock | overlay inmp441-bare |
| WS / LRCK | GPIO 19 | Word select | overlay inmp441-bare |
| L/R | GPIO 21 | Selector de canal | libgpiod; LOW = left (default) |

**Regla crítica:** el overlay I2S **debe** estar activo en `/boot/config.txt`:
`dtoverlay=inmp441-bare` (compilado desde `overlays/inmp441-bare.dts`). No debe
haber OTROS overlays I2S (`googlevoicehat-soundcard`, `i2s-mems-mic`, etc.) que
peleen por el mismo periférico. El kernel es quien posee el periférico PCM/I2S.

## 3. Arquitectura de código

```
src/
  main.cpp                          # CLI, menú interactivo, modos
  audio/
    AlsaDeviceFinder.cpp/hpp        # Auto-detección tarjeta ALSA "bare"
    INMP441.cpp/hpp                 # Capa micrófono sobre ALSA, 24-bit alignment
    AudioProcessor.cpp/hpp          # RMS, WAV writer, meter ASCII
  core/
    Config.cpp/hpp                  # Parsing CLI + JSON persistido (--alsa-device/--gpio-chip)
    Logger.cpp/hpp                  # Logging a stderr (thread-safe)
    SignalHandler.cpp/hpp           # Ctrl+C / SIGTERM cooperativo
  oled/
    oled_display.cpp/hpp            # SSD1306 por I2C
  sound/
    player.cpp/hpp                  # Playback PulseAudio (libao)
    track_list.cpp/hpp              # Escaneo de output/
  tools/
    bluetooth_tool.cpp/hpp          # bluetoothctl + pactl A2DP
```

### Flujo de datos

```
INMP441 (24-bit I2S) → ALSA / kernel I2S driver (S32_LE, raw 32-bit slots)
    → INMP441::readFrames() → AudioFrame { left24, right24 }
    → AudioProcessor / recordWavToFile():
        1. rawToSample24() (>> 8 aritmético)
        2. HPF one-pole (opcional, por canal)
        3. Ganancia digital dB→lineal
        4. sample24ToSample16() (>> 8)
        5. WaveWriter::writeFrames16() → WAV 16-bit PCM mono
```

## 4. Modos de ejecución

| Modo | Flag | Descripción |
|---|---|---|
| menú interactivo | `--menu` (default) | Presentación + menú duración/canal/formato/grabación/level |
| level meter | `--level` | RMS/peak en dBFS en terminal, refresco configurable |
| WAV | `--wav [archivo]` | Graba N segundos a WAV 16-bit PCM mono |
| MP3 | `--mp3 [archivo]` | Graba WAV temp → lame 128 kbps CBR 44.1 kHz mono |
| player BT | `--player --bt-mac <MAC>` | Lista `output/`, conecta speaker BT A2DP, UI teclas raw |
| dump | `--dump [N]` | Muestra N slots 32-bit crudos (verificación wiring) |
| info | `--info` | Hardware y configuración |

## 5. Configuración CLI y persistida

Archivo por defecto: `config.json` (gitignored).

```json
{
  "bt_mac": "AC:EF:92:D0:B5:BB",
  "dropout_seconds": 1.0,
  "duration_seconds": 2.0,
  "format": "mp3",
  "gain_db": 49.0,
  "left_channel": false,
  "meter_interval_ms": 120.0,
  "sample_rate": 48000,
  "stereo": false,
  "warmup_seconds": 4.0,
  "hpf_hz": 30.0
}
```

- CLI siempre sobrescribe el archivo.
- `--alsa-device <dev>` (default "default" = auto-detecta la tarjeta "bare";
  alternativas `plughw:<card>,0`) y `--gpio-chip <chip>` (default gpiochip0)
  son solo CLI, no se persisten en el JSON.
- El menú guarda automáticamente en cada cambio.
- `--save-config` persiste los flags CLI al archivo.

## 6. Pipeline de grabación (recordWavToFile)

```cpp
// 1. Warmup: descarta N segundos (default 4 s) para eliminar el transitorio
//    de arranque del I2S y del micrófono.
if (config.warmupSeconds > 0.0) { descartar frames... }

// 2. Por cada frame leído:
//    a) Convertir 24-bit → 16-bit: sample24ToSample16()
//    b) HPF one-pole (por canal, antes de la ganancia)
//    c) Ganancia digital: gain = 10^(dB/20), aplicada en dominio 16-bit
//    d) Clamp a [-32768, 32767]
//    e) Detección de dropouts (muestras consecutivas en cero)
//    f) writeFrames16() → WAV
```

### Mejoras implementadas (v1.7.9)

- **LPF (low-pass filter):** Agregado filtro paso-bajo configurable (`--lpf <Hz>`,
  opción 9 en el menú). Permite eliminar ruido de alta frecuencia y la respuesta
  ultrasónica del INMP441. Valores recomendados: 8000–12000 Hz.
- **Bug stereo corregido:** El modo `--stereo` hacía `left16() | right16()` para
  la detección de dropouts, destruyendo la información de ambos canales. Ahora
  la detección usa el canal activo y el interleaving es correcto.
- **Fix compilación (v1.7.10):** Agregado `#include <cmath>` faltante en
  `AudioProcessor.hpp` para `std::exp` usado por el LPF one-pole.

## 7. Problema conocido: señal tenue y ruidosa en grabaciones

### Síntomas reportados
- Primeros segundos en silencio (transitorio de arranque).
- Luego la señal es muy baja y con ruido de fondo elevado.
- Se mencionó agregar una opción para "no grabar los agudos" (filtro paso-bajo).

### Causas probables (pre-v1.7.9)

1. **INMP441 de bajo nivel de salida:** El INMP441 tiene salida de señal muy baja
   (sensibilidad ~ -26 dBFS a 94 dB SPL). Requiere ganancia digital alta
   (ej: +30 a +49 dB) para niveles de conversación normales. Si la ganancia
   es insuficiente, el audio queda tenue.

2. **Ruido de power rail / sub-bass:** El HPF de 30 Hz remueve el DC offset y
   el hum de la fuente de alimentación, pero si el ruido está en bandas medias
   o altas (ripple de switching, ruido térmico), el HPF no lo elimina.

3. **Ruido de alta frecuencia:** El micrófono captura ruido eléctrico y
   ambiental en bandas altas. Un filtro paso-bajo (LPF) ayuda a reducirlo.

4. **Warmup insuficiente:** Si `warmup_seconds` es muy bajo, el transitorio
   de arranque del PCM/I2S y del propio INMP441 se cuela en la grabación
   (silencio inicial o clic).

5. **Clip por ganancia excesiva:** Ganancias muy altas sin HPF previo pueden
   saturar y generar distorsión dura, que se percibe como "ruido".

### Soluciones disponibles (v1.7.9+)

- **LPF (`--lpf <Hz>`, opción 9 en menú):** Filtro paso-bajo configurable.
  Usar valores entre 8000–12000 Hz para reducir el hiss de alta frecuencia.
- **HPF (`--hpf <Hz>`, opción 8):** Filtro paso-alto configurable (default 30 Hz).
- **Ganancia alta (`--gain <dB>`):** El INMP441 requiere ganancias de +30 a +49 dB
  para niveles normales de conversación.
- **Warmup (`--warmup <SEC>`):** Default 4 segundos; aumentar si hay clics
  iniciales.

## 8. Convenciones del proyecto

### Commits (Conventional Commits)
```
feat(scope): descripción
fix(scope): descripción
docs: ...
build: ...
chore: ...
refactor: ...
test: ...
```

### Versionado
- **Todo push debe tener su tag.**
- `VERSION` siempre coincide con el último tag (sin `v`).
- Ciclo patch 0-9 obligatorio: `v1.7.7` → siguiente es `v1.7.8` (no `v1.8.0`).
- No se eliminan ni reemplazan tags publicados.
- Cada commit significativo recibe un tag incremental.

### Git hooks
- `commit-msg` valida el formato de commit.
- Instalar con: `bash scripts/install_commit_hook.sh`

### .gitignore
- Incluye `PROMPT.md` (documento de tracking del usuario).
- `config.json` está ignorado (configuración local).

## 9. Build y dependencias

```bash
# En la Pi
sudo bash scripts/install_dependencies.sh
make clean && make -j4
./bin/inmp441_rpi --info

# Tests (sin hardware)
make test

# Cross-build (x86_64 → armhf)
bash scripts/cross_build.sh
# Requiere GCC 10 (g++-10-arm-linux-gnueabihf), auto-detectado
```

Dependencias instaladas por el script:
- `build-essential`, `git`, `wget`, `curl`
- `nlohmann-json3-dev` (config JSON)
- `libasound2-dev`, `libgpiod-dev`, `pkg-config` (captura ALSA + L/R select; sin root)
- `libmpg123-dev`, `libao-dev` (playback)
- `lame` (encoding MP3)
- `bluez`, `pulseaudio`, `pulseaudio-module-bluetooth`, `pulseaudio-utils`
- `bcm2835` v1.71 (solo para el display OLED)

## 10. Bluetooth / Player

- Usa `bluetoothctl` para conectar y `pactl` para controlar PulseAudio.
- Política estricta: un solo dispositivo configurado con `--bt-mac`.
- Si el speaker no está disponible, el playback se aborta (no hay fallback
  a altavoz local).
- El proceso baja privilegios de root al usuario real para que PulseAudio
  funcione (PulseAudio rechaza root).
- OLED (SSD1306 por I2C) se inicializa antes de bajar privilegios porque
  `bcm2835` necesita root.

## 11. Estructura de directorios relevante

```
inmp441_rpi/
├── Makefile
├── README.md
├── LICENSE
├── VERSION                      # coincide con el último tag (sin v)
├── .gitignore                   # incluye PROMPT.md y config.json
├── include/
│   ├── core/
│   │   ├── Config.hpp
│   │   ├── Logger.hpp
│   │   └── SignalHandler.hpp
│   ├── audio/
│   │   ├── SampleFormat.hpp
│   │   ├── AlsaDeviceFinder.hpp
│   │   ├── INMP441.hpp
│   │   └── AudioProcessor.hpp
│   ├── oled/
│   └── sound/
├── src/
│   ├── main.cpp
│   ├── core/
│   ├── audio/
│   ├── oled/
│   ├── sound/
│   └── tools/
├── obj/                         # gitignored, espejo de src/
├── bin/                         # gitignored, binario final
├── docs/
│   ├── README.md
│   ├── hardware_setup.md
│   ├── architecture.md
│   ├── build_and_install.md
│   ├── usage.md
│   ├── testing.md
│   └── LEARNINGS.md
├── scripts/
│   ├── install_dependencies.sh
│   ├── cross_build.sh
│   ├── install_commit_hook.sh
│   ├── run.sh
│   └── check_prompt.sh
├── tests/
│   └── test_conversion.cpp
└── output/                      # gitignored, grabaciones
```

## 12. Referencias rápidas

| Concepto | Detalle |
|---|---|
| Overlay I2S | `dtoverlay=inmp441-bare` (driver del kernel, dmic-codec) |
| Dispositivo ALSA | `plughw:<card>,0`, tarjeta detectada por nombre "bare" |
| Formato ALSA | S32_LE, 2 canales, ~48 kHz (24 bits MSB-alineados) |
| L/R select | libgpiod, gpiochip0 línea 21; LOW = left, HIGH = right |
| Root | No requerido para grabar; solo para OLED (bcm2835, /dev/mem) |
| Sample alignment | 24-bit en bits 31-8 del slot → `raw >> 8` |
| HPF one-pole | `y[n] = x[n] - x[n-1] + R*y[n-1]`, R = `1 - 2*pi*fc/fs` |
| Dropout | N muestras consecutivas en cero → evento reportado |
| Tags | Inmutables; no se eliminan ni reemplazan |

## 13. Checklist de items PROMPT.md (TODO.md)

| # | Item | Estado | Versión |
|---|---|---|---|
| 1 | Pantalla player: versión, segundos, barra, extensión | ✅ Hecho | v1.7.5 |
| 2 | Segfault menú grabador (`Choice>`) | ✅ Hecho | v1.7.7 |

## 14. Errores comunes y soluciones

| Error | Causa | Solución |
|---|---|---|
| `403 Permission denied` en push | Credenciales sin permisos | Usar token con `repo` scope en config global |
| No hay datos (silencio/xrun ALSA) | Overlay no cargado o BCLK/WS no llegan al mic | Verificar `dtoverlay=inmp441-bare` y wiring GPIO 18/19/20 |
| Señal quieta o half-scale | Alineación de muestras | Corroborar `--dump`; ajustar data-delay en `overlays/inmp441-bare.dts` |
| L/R invertido | Pin L/R con lógica opuesta | Cambiar `--channel` o cableado |
| Ruido constante | L/R flotando | Hard-wire a GND (left) o 3V3 (right) |
| Segfault en menú (cross build) | ABI incompatible GCC 13 vs libstdc++ 6 | Usar GCC 10 para cross-compilar |
