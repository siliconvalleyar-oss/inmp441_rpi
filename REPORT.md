# REPORT — Grabación de audio baja y con ruido (INMP441)

## ⚠️ REGLA DE USO (obligatoria)

> **Quien lea este reporte (humano o IA) debe completarlo a medida que lo cumple:**
> marcar `[x]` cada punto del checklist cuando se verifique/ejecute, anotar el
> resultado en la tabla de pruebas (§6) y actualizar la sección **Estado** (§1).
> **No se cierra el reporte con items del checklist sin marcar.** Cada sesión
> que toque el tema empieza leyendo este archivo y continúa el checklist donde
> quedó.

---

## 1. Estado

| Campo | Valor |
|---|---|
| Síntoma reportado | Grabaciones se escuchan **muy bajas** y **con ruido** |
| Versión de la app | v2.0.1 (VERSION = 2.0.1) |
| Fecha de creación del reporte | 2026-08-16 |
| Estado global | ⏳ En diagnóstico (checklist en curso) |
| Última actualización | 2026-08-16 |

---

## 2. Resumen ejecutivo

El INMP441 es un micrófono **muy poco sensible por diseño** (−26 dBFS a 94 dB SPL,
SNR 61 dBA). Tres causas dominantes explican "bajo + ruido":

1. **Ganancia digital en 0 dB por defecto** (`Config.hpp: gainDb = 0.0`): sin
   `--gain` (o sin config.json con gain alto), el habla normal queda a
   **~−50/−60 dBFS**. Se necesita **+20 a +40 dB**.
2. **El LPF está OFF por defecto** (`Config.hpp: lpfHz = 0.0`): sin filtro
   paso-bajo, el hiss de alta frecuencia y la respuesta ultrasónica del mic
   (hasta ~50 kHz) se graban tal cual.
3. **La ganancia amplifica el piso de ruido**: con +40 dB, el ruido propio del
   mic (~−87 dBFS A) y el ruido del riel 3.3 V de la Pi suben a niveles
   audibles. El ruido se arregla **subiendo la señal** (distancia, alimentación
   limpia, capacitor de desacople, filtros), no solo con más ganancia.

**Solución combinada mínima**: `--gain 30 --lpf 10000 --hpf 30` (o presets del
menú, opción N) + capacitor 100 nF entre VDD y GND + cables cortos.

---

## 3. Análisis de causas

### 3.1 Por qué el audio suena BAJO

| # | Causa | Explicación | Evidencia en el código | Solución |
|---|---|---|---|---|
| 1 | **Sensibilidad intrínseca del INMP441** | −26 dBFS @ 94 dB SPL, 1 kHz. Habla normal (~60-70 dB SPL) ⇒ señal cruda ≈ −50/−60 dBFS. Es física del componente, no un bug. | `docs/hardware_setup.md` (datasheet); SKILLS.md §7 | Ganancia digital +20..+40 dB; acercar el mic; hablar más fuerte |
| 2 | **Gain en 0 dB por defecto** | Sin `--gain` ni `gain_db` en config.json, la grabación sale sin amplificar. | `include/core/Config.hpp:31` (`gainDb = 0.0`) | `--gain 30` o menú opción 3; el menú guarda en config.json |
| 3 | **Alineación de muestras (`raw >> 8`)** | Si el driver entrega los 24 bits en otra posición (p. ej. 1 bit de data-delay distinto), `raw >> 8` devuelve **la mitad** del valor (−6 dB). | `include/audio/SampleFormat.hpp` (`rawToSample24`); `src/audio/INMP441.cpp` | `--dump 64`: silencio ⇒ 0; aplauso ⇒ swings grandes. Si el pico máximo ≈ ±0x3FFFFF y no ±0x7FFFFF, ajustar data-delay en `overlays/inmp441-bare.dts` |
| 4 | **Canal equivocado (slot vacío)** | Si el mic transmite por el slot derecho y se lee el izquierdo (o viceversa), se graba el slot **en silencio**. | `--channel` / `selectLeftChannel` en `main.cpp`; L/R por GPIO21 | Verificar que `--channel` coincide con el cableado L/R; `--dump` muestra en qué slot está la señal |
| 5 | **Dropouts (silencio digital)** | Si el mic o el riel se caen intermitentemente, parte de la grabación es silencio ⇒ nivel promedio bajo. | `main.cpp:339` (`MIC DROPOUT`), retry por silencio (`silentRetryFraction`) | Revisar el resumen de grabación ("AUDIO DROPOUTS"); capacitor + alimentación estable |
| 6 | **Estéreo con 1 mic** | Con `--stereo`, el slot vacío es cero; al reproducir/mezclar, la mitad de la señal es silencio. | `recordStereo` en `Config.hpp` | Usar mono (default) salvo que haya 2 mics |

### 3.2 Por qué suena con RUIDO

| # | Causa | Explicación | Evidencia en el código | Solución |
|---|---|---|---|---|
| 1 | **LPF OFF por defecto** | `lpf_hz = 0` ⇒ el hiss de alta frecuencia y la respuesta ultrasónica del INMP441 (hasta ~50 kHz) se graban. Es el **mayor lever de software** y está apagado por defecto. | `include/core/Config.hpp:33` (`lpfHz = 0.0`) | `--lpf 8000..12000` (menú opción 9) o presets N |
| 2 | **Ganancia amplificando el piso de ruido** | SNR 61 dBA ⇒ piso ≈ −87 dBFS(A). Con +40 dB ⇒ ruido audible a ~−47 dBFS. | `main.cpp` `applyFx` (gain en dominio 24-bit/float) | Subir la **señal** (distancia/alimentación), no solo el gain; HPF antes del gain (ya implementado) |
| 3 | **Ruido del riel 3.3 V (switching) / falta de desacople** | El regulador de la Pi inyecta ripple/hash. Sin **capacitor 100 nF entre VDD y GND** cerca del mic, el ruido entra directo. | `docs/hardware_setup.md` (troubleshooting: "No decoupling... erratic readings") | Soldar 100 nF cerámico lo más cerca posible del VDD; alimentación limpia (ferrita/RC en VDD) |
| 4 | **L/R flotando** | Con `--no-lr-gpio` y L/R sin cablear, el pin flota ⇒ ruido constante / amplitud baja. | `docs/hardware_setup.md` ("Constant noise / low amplitude") | Cablear L/R a GND (left) o 3V3 (right), o usar GPIO21 (default) |
| 5 | **Cables largos / breadboard** | BCLK ≈ **3.072 MHz** (48 kHz × 64). Cables largos y breadboard captan interferencia y deforman los flancos I2S ⇒ errores de bit ⇒ ráfagas de ruido. | `main.cpp` `runInfoMode` (BCLK = rate × 64) | Cables < 10-15 cm; retorcer SCK/WS; mantener SD alejado de fuentes de ruido |
| 6 | **Clipping por sobre-ganancia** | Gain excesivo ⇒ saturación dura ⇒ distorsión percibida como ruido. | `main.cpp:388` (`CLIPPING`, sugiere bajar 6 dB) | Ajustar gain para picos < −6 dBFS; revisar el resumen |
| 7 | **HPF insuficiente para hum del riel** | El HPF de 30 Hz quita DC y sub-bass, pero el hum del riel puede estar a 50/100 Hz. | `include/core/Config.hpp:32` (`hpfHz = 30.0`) | Subir HPF a 60-100 Hz (opción 8) si el ruido es grave |
| 8 | **Ruido ambiental / RF (WiFi-BT)** | La Zero 2 W integra WiFi/BT 2.4 GHz; la línea SD puede captar hash. | — | LPF ayuda; blindaje/apantallado del cable SD |

---

## 4. Checklist de diagnóstico (en la Pi)

Ejecutar en orden; marcar `[x]` y anotar resultados en §6.

- [ ] `./bin/inmp441_rpi --info` — confirmar rate, canal y backend
- [ ] `./bin/inmp441_rpi --dump 64` — silencio ⇒ slots ≈ 0; aplauso ⇒ swings grandes en el **slot correcto**
- [ ] Verificar pico máximo del slot: ¿llega a ±0x7FFFFF o solo a ±0x3FFFFF (desalineación)?
- [ ] ¿La señal está en el slot **left** o **right**? ¿Coincide con `--channel`?
- [ ] `./bin/inmp441_rpi --level` — hablar a 20-30 cm: el RMS debe subir bien por encima del piso (−60 dBFS)
- [ ] Grabar de control: `--wav test_gain0.wav -d 5` (gain 0) y `--wav test_gain30.wav -d 5 --gain 30 --lpf 10000`
- [ ] Comparar con `sox test_*.wav -n stat` (RMS/peak de cada uno)
- [ ] Revisar el resumen de cada grabación: ¿DROPOUTS? ¿CLIPPING?
- [ ] Confirmar `dtoverlay=inmp441-bare` activo y sin overlays I2S en conflicto (`arecord -l`)
- [ ] Verificar con `arecord -D plughw:<card>,0 -f S32_LE -r 48000 -c 2 -d 5 /tmp/raw.wav` que la captura cruda es estable

## 5. Checklist de corrección

### 5.1 Hardware (en la Pi)
- [ ] Soldar capacitor **100 nF cerámico entre VDD y GND**, lo más cerca posible del mic
- [ ] Acortar cables: SD/BCLK/WS < 10-15 cm; GND sólido y corto
- [ ] L/R: cableado a GND (left) o 3V3 (right), o verificar que GPIO21 se está manejando (no usar `--no-lr-gpio` sin cablear)
- [ ] Verificar wiring: SD→GPIO20 (pin 38), BCLK→GPIO18 (pin 12), WS→GPIO19 (pin 35), VDD→3.3 V
- [ ] (Opcional) Alimentar el mic desde 3.3 V limpio o agregar ferrita/RC en VDD

### 5.2 Software / configuración (en la Pi o local)
- [ ] `--gain 30` (o +24..+40 según distancia) — menú opción 3
- [ ] `--lpf 10000` (8000-12000) — menú opción 9
- [ ] `--hpf 30` (subir a 60-100 si hay hum grave) — menú opción 8
- [ ] `--warmup 4` (default; subir si hay clics iniciales)
- [ ] Si el pico del `--dump` es ±0x3FFFFF: revisar data-delay en `overlays/inmp441-bare.dts` y reconstruir el overlay
- [ ] Persistir: `--save-config` o dejarlo guardado desde el menú (config.json)
- [ ] Probar los presets del menú (opción N: Light/Medium/Strong) y elegir el que suene más limpio

---

## 6. Registro de pruebas (completar a medida que se ejecutan)

| Fecha | Prueba / comando | Resultado | Acción tomada |
|---|---|---|---|
| 2026-08-16 | Análisis de código (defaults gain=0, LPF=0) | Confirmado: sin gain ni LPF el audio sale bajo y con hiss | Se documenta en §3; pendiente verificación en la Pi |
| _fecha_ | _ej. `--dump 64`_ | _pico máximo = 0x?FFFFF, slot = L/R_ | _ej. ajustar overlay_ |

---

## 7. Referencias

- `docs/hardware_setup.md` — wiring, datasheet del INMP441, troubleshooting (capacitor, L/R flotante, alineación)
- `docs/usage.md` — opciones `--gain/--hpf/--lpf/--dump/--level`, alineación de muestras
- `docs/SKILLS.md` §7 — problema conocido "señal tenue y ruidosa" (pre-v1.7.9) y soluciones
- `docs/TODO.md` — checklist de pedidos de PROMPT.md
- `scripts/diag_mic.sh` — script de diagnóstico que resume el log (L/R, warmup, dropouts, clipping)
- `src/main.cpp` — `recordWavToFile()` (pipeline: warmup → HPF → LPF → gain → 16-bit), resumen de DROPOUTS/CLIPPING
- `include/core/Config.hpp` — defaults de configuración
