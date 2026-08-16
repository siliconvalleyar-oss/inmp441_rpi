# Grabador y reproductor INMP441 para Raspberry Pi

Graba audio desde un micrófono I2S INMP441, lo guarda en `output/*.wav`, y
lo reproduce desde un menú interactivo por consola. Funciona sin cambios
tanto en **Raspberry Pi 4 (64-bit)** como en **Raspberry Pi Zero 2W
(32-bit)**: el `Makefile` no fija ninguna arquitectura, cada Pi compila
nativo con su propio `g++`.

## Por qué el diseño es así

El BCLK/WS/SD del INMP441 **no se manejan por software**: se conectan al
periférico I2S de hardware del SoC (GPIO18/19/20 son justo los pines fijos
de `PCM_CLK` / `PCM_FS` / `PCM_DIN`). El kernel de Raspberry Pi OS expone
ese periférico como una tarjeta de sonido ALSA; por eso la captura de audio
se hace con `libasound` (ALSA), no bit-bangueando GPIO. Bit-banguear I2S
por software sería inestable a las frecuencias de reloj que necesita el
INMP441 y no escalaría bien en una Pi Zero 2W.

GPIO21 es distinto: el INMP441 sólo lo **lee como nivel estático** para
decidir si sus muestras van al slot izquierdo o derecho del frame I2S, no
es parte del streaming en tiempo real. Por eso ese pin sí se controla
directo con GPIO (vía `libgpiod`, la API moderna y la que sigue viva tanto
en Bullseye/64-bit como en versiones más viejas de 32-bit; `wiringPi` está
deprecado y no conviene para código nuevo).

## Estructura del proyecto

```
inmp441_recorder/
├── Makefile
├── include/
│   ├── audio/                     # Captura/reproducción PCM + codec WAV
│   │   ├── alsa_capture.hpp       #   Captura PCM vía ALSA
│   │   ├── alsa_device_finder.hpp #   Descubrimiento de dispositivos ALSA
│   │   ├── alsa_playback.hpp      #   Reproducción PCM vía ALSA
│   │   ├── wav_reader.hpp         #   Lectura de .wav (para reproducir)
│   │   └── wav_writer.hpp         #   Escritura incremental de .wav
│   ├── core/
│   │   └── file_utils.hpp         # Listado de output/*.wav
│   └── gpio/
│       └── gpio_channel_select.hpp  # Selección L/R por GPIO (libgpiod)
├── src/                          # un .cpp por cada .hpp, en su mismo subdirectorio
│   ├── audio/
│   │   ├── alsa_capture.cpp
│   │   ├── alsa_device_finder.cpp
│   │   ├── alsa_playback.cpp
│   │   ├── wav_reader.cpp
│   │   └── wav_writer.cpp
│   ├── core/
│   │   └── file_utils.cpp
│   ├── gpio/
│   │   └── gpio_channel_select.cpp
│   └── main.cpp                   # orquestación y menú interactivo
├── output/                       # acá se guardan y desde acá se listan los .wav
├── obj/                          # (generado por make) .o, replicando src/
└── bin/                          # (generado por make) binario final
```

Cada pieza es independiente (captura / reproducción / GPIO / archivo), así
que si mañana querés cambiar el destino del audio (por ejemplo, streamear
por red en vez de grabar a disco), sólo tocás `main.cpp` y reusás
`AlsaCapture` / `AlsaPlayback` / `GpioChannelSelect` tal cual.

`make` compila cada `src/<sub>/archivo.cpp` a `obj/<sub>/archivo.o`
respetando la jerarquía de carpetas: el `Makefile` descubre las fuentes con
`find src -name '*.cpp'` y replica la jerarquía automáticamente en `obj/`
sin tocar nada al agregar subcarpetas.

## 1. Configurar la Raspberry Pi (una sola vez)

Editar el archivo de configuración de arranque:

- Raspberry Pi OS Bullseye/Bookworm recientes: `/boot/firmware/config.txt`
- Versiones más viejas: `/boot/config.txt`

Agregar (o descomentar) estas líneas:

```
dtparam=i2s=on
dtoverlay=inmp441-bare
```

`inmp441-bare` es un overlay custom para micrófonos I2S esclavos tipo INMP441
en el pinout por defecto (GPIO18/19/20/21). A diferencia de
`googlevoicehat-soundcard` (que deja la CPU I2S como esclava y nunca genera el
reloj), `inmp441-bare` pone la **CPU como master** de BCLK/WS: el bus se
activa y el INMP441 entrega datos. El DTS está en `overlays/inmp441-bare.dts`
del repo; el `.dtbo` se compila con `dtc` y se copia a
`/boot/firmware/overlays/`. Después de editar, reiniciar:

```bash
sudo reboot
```

Verificar que la tarjeta aparezca:

```bash
arecord -l
```

Debería listar algo como `card 1: inmp441bare [inmp441-bare]` o similar.
Anotá el número de card/device (ej. `hw:1,0`), lo vas a usar con `-d`.

## 2. Instalar dependencias de compilación

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
    libasound2-dev libgpiod-dev
```

## 3. Compilar

```bash
cd inmp441_recorder
make
```

Esto compila cada `.cpp` de `src/` a `obj/`, linkea y genera:

```
bin/inmp441_recorder
```

Ya probado que compila limpio con `-Wall -Wextra`, tanto en release como
en debug (`make DEBUG=1`, sin optimizar y con símbolos `-g3`).

Otros targets útiles:

```bash
make run        # compila y abre el menú interactivo
make clean       # borra bin/ y obj/
make distclean   # clean + borra también output/ (¡cuidado, borra grabaciones!)
make install     # instala el binario en /usr/local/bin
make help        # lista todos los targets
```

## 4. Permisos de GPIO

Para no correr todo como root, agregá tu usuario al grupo `gpio`:

```bash
sudo usermod -aG gpio $USER
# cerrar sesión y volver a entrar para que aplique
```

## 5. Usar

### Menú interactivo (recomendado para uso manual)

Ejecutar sin argumentos abre un menú por consola para grabar y **reproducir**
lo grabado, listando automáticamente lo que hay en `output/`:

```bash
./bin/inmp441_recorder
```

```
=== Grabador INMP441 ===
1) Grabar nuevo audio
2) Reproducir una grabación
3) Listar grabaciones
4) Salir
```

Las grabaciones se guardan siempre en `output/`, con nombre automático
tipo `grabacion_20260814_153000.wav` si no se especifica uno.

### Línea de comandos (para scripts / systemd / cron)

```bash
# Grabar hasta Ctrl+C, 48kHz/32bit, canal izquierdo, dispositivo hw:0,0
./bin/inmp441_recorder -d hw:0,0

# Grabar 10 segundos exactos, con nombre propio (se guarda en output/)
./bin/inmp441_recorder -d hw:0,0 -o clip.wav -t 10

# Canal derecho (si tenés el pin L/R en alto, o dos micrófonos en estéreo)
./bin/inmp441_recorder -d hw:0,0 -c R

# Listar grabaciones existentes en output/
./bin/inmp441_recorder -l

# Reproducir una grabación puntual y salir (sirve para integrarlo al
# "menú" de otra app: sólo hay que invocar el binario con -p)
./bin/inmp441_recorder -p grabacion_20260814_153000.wav
```

Opciones completas: `./bin/inmp441_recorder -h`

### Integrarlo al menú de otra aplicación

Si "el menú de la app" es otra aplicación (una GUI, un script Bash, un
panel web, etc.) y no este mismo binario, alcanza con que esa app:

1. Liste los archivos de `output/*.wav` (por convención, siempre están ahí).
2. Para reproducir uno, llame a `./bin/inmp441_recorder -p <archivo>.wav`
    (o directamente `aplay output/<archivo>.wav`, ya que son WAV PCM estándar
    reproducibles por cualquier reproductor de audio de Linux).

## Escalar a estéreo (dos INMP441)

Si más adelante conectás dos INMP441 al mismo bus (uno con L/R a GND y
otro a VDD) y configurás el overlay/driver para 2 canales, sólo hay que
ajustar `cap_cfg.num_channels = 2;` en `main.cpp` (o exponerlo como flag
`-ch`) — el resto del pipeline (ALSA, WAV writer) ya soporta N canales sin
cambios.
