# TODO — Checklist de items de PROMPT.md

Seguimiento de los pedidos del usuario (`PROMPT.md`) con verificación de lo
implementado. Se actualiza en cada pasada del loop:

```bash
bash scripts/check_prompt.sh   # detecta items nuevos en PROMPT.md
```

| # | Item (PROMPT.md) | Estado | Versión | Commit | Verificado |
|---|---|---|---|---|---|
| 1 | Pantalla del player: versión, segundos, barra de progreso, extensión | ✅ Hecho | 1.7.5 | `1ad7d48` | Pi real (terminal + OLED) |
| 2 | Segfault del menú del grabador (`Choice> Segmentation fault`) | ✅ Hecho | 1.7.7 | `abbdf79` | Pi real (menú, grabación, player) |

---

## 1. Pantalla del player: versión, segundos, barra, extensión

**Pedido:** que en la reproducción del tema se vean la versión de la aplicación,
los segundos transcurridos, un progress bar y la extensión del archivo.

- [x] Versión de la app visible en la pantalla del player (`=== Playback v1.7.x ... ===`)
- [x] Segundos transcurridos / duración del tema (`0:42 / 2:15`, `--:--` si es desconocida)
- [x] Barra de progreso ASCII de 24 celdas (`[####------]`)
- [x] Extensión del archivo visible en la lista (`.wav` / `.mp3`)
- [x] Espejo en el OLED (fila 2 = barra, fila 3 = tiempo + versión)
- **Implementado:** `1ad7d48` (v1.7.5); docs en `87aac5b`
- **Verificado en la Pi:** player por Bluetooth (Xiaomi Sound Pocket), barra
  avanzando en tiempo real, extensión visible, OLED activo, navegación entre
  pistas OK

## 2. Segfault del menú del grabador (`Choice>`)

**Pedido:** el menú del grabador se cae con Segmentation fault al ejecutar.

- [x] Crash reproducido (`EXIT_STATUS=11` = SIGSEGV, igual con entrada "0" o "6")
- [x] Diagnóstico: crash en `std::getline` (`main.cpp:791`) solo en binarios cruzados
- [x] Causa raíz: build cruzado GCC 13 (headers libstdc++ 13) enlazando la
      libstdc++ 6.0.28 (GCC 10) de la Pi → ABI incompatible → corrupción en iostreams
- [x] Fix: `cross_build.sh` exige GCC 10 (`g++-10-arm-linux-gnueabihf`), auto-detecta
      el toolchain (PATH / `~/gcc10-cross` / `/mnt/disk/gcc10-cross` / `/opt/gcc10-cross`)
- [x] Verificado en la Pi: menú v1.7.7 sin crash (`Choice> Bye.`), grabación 12 s → MP3
      guardado sin dropouts, player OK (OLED + BT + barra)
- [x] Binario y VERSION 1.7.7 desplegados en la Pi
- **Implementado:** `abbdf79` (v1.7.7)

---

## Pendientes operativos (fuera del PROMPT)

- [x] Limpiar pistas de prueba de `output/` en la Pi (010 / 011 / 012 / 013) — eliminadas
- [x] Revisar el `stash` viejo en el repo de la Pi — son WIP ya superado
      (stash@{0} = pantalla del player, commiteada como `1ad7d48`;
      stash@{1} = build/docs, ya en el historial). Se dejaron sin borrar
      (borrarlos es destructivo); se pueden dropear con `git stash drop`
- [x] Eliminar headers `c++/10` sin uso que quedaron en el sysroot
      (el toolchain GCC 10 trae los suyos) — borrados `usr/include/c++` de
      `/mnt/disk/arm-sysroot` (12 MB). Verificado: la traza `-H` de
      `<vector>`/`<iostream>` resuelve a `gcc10-cross/.../include/c++/10` y
      los 15 fuentes compilan con `-fsyntax-only` con los flags exactos del
      cross-build sin esa carpeta. Nota: el cross-build no llegó a linkear
      porque el sysroot carece de las libs runtime de audio
      (libasound/libgpiod/libmpg123/libao) — gap preexistente, ajeno a esta
      limpieza
