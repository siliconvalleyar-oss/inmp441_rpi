#!/usr/bin/env bash
# ============================================================================
# diag_mic.sh - Diagnostico completo del microfono INMP441
#
# Pruebas (todas en estereo y gain 0, para analizar AMBOS slots I2S):
#   A  GPIO21 ON, canal configurado    -> baseline
#   B  GPIO21 SIN tocar (--no-lr-gpio) -> el driver mata al mic?
#   C  GPIO21 ON, canal opuesto        -> el slot real cambia?
#   D  Warmup normal                   -> cuanto tarda en "despertar"
#   E  Grabacion larga continua        -> dropouts a mitad de captura
#
# Responde: slot real del mic, si la config de canal es correcta, si el
# driver de GPIO21 es un problema, cuanto tarda en dar senal viva y cuan
# severos son los apagones (dropouts) de senal.
#
# Uso:
#   sudo ./scripts/diag_mic.sh              diagnostico completo
#   sudo ./scripts/diag_mic.sh -d 6         pruebas cortas (A/B/C/D) de 6 s
#   sudo ./scripts/diag_mic.sh -D 45        prueba larga (E) de 45 s
#   ./scripts/diag_mic.sh -n                solo analizar la ultima corrida
#   ./scripts/diag_mic.sh -c                borrar debug_*.wav / debug_*.log
#   ./scripts/diag_mic.sh -t 0.5            umbral de dropout de 0.5 s
#
# Los WAV quedan en output/ como debug_<AAAAmmddHHMMSS>_<test>.wav
# ============================================================================
set -u

cd "$(dirname "$0")/.."   # raiz del repo (donde estan bin/ y config.json)

APP=${APP:-"./bin/inmp441_rpi"}
OUT=${OUT:-output}
SHORT_DUR=${SHORT_DUR:-8}        # duracion de A/B/C/D
LONG_DUR=${LONG_DUR:-30}         # duracion de E
DROP_THRESH=${DROP_THRESH:-0.25} # umbral de dropout para el analisis (s)
NORECORD=0
CLEAN=0

usage() {
  sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [ $# -gt 0 ]; do
  case "$1" in
    -d) SHORT_DUR="$2"; shift 2 ;;
    -D) LONG_DUR="$2"; shift 2 ;;
    -t) DROP_THRESH="$2"; shift 2 ;;
    -n) NORECORD=1; shift ;;
    -c) CLEAN=1; shift ;;
    -h|--help) usage ;;
    *) echo "Opcion desconocida: $1  (-h para ayuda)"; exit 1 ;;
  esac
done

[ -x "$APP" ] || { echo "ERROR: no existe $APP (compile primero: make)"; exit 1; }
command -v python3 >/dev/null || { echo "ERROR: se necesita python3"; exit 1; }
mkdir -p "$OUT"

if [ "$CLEAN" = 1 ]; then
  rm -f "$OUT"/debug_*.wav "$OUT"/debug_*.log
  echo "Borrados los archivos de diagnostico en $OUT/"
  exit 0
fi

VER=$("$APP" --version 2>/dev/null | head -1)
echo "== diagnostico INMP441 =="
echo "  App            : $APP ($VER)"
echo "  Pruebas cortas : ${SHORT_DUR}s (A/B/C/D)   Larga: ${LONG_DUR}s (E)   Umbral dropout: ${DROP_THRESH}s"
[ "$(id -u)" -eq 0 ] || echo "  (grabar requiere root: cada prueba usara sudo)"

echo "== config actual =="
if [ -f config.json ]; then
  python3 - <<'PY'
import json
c = json.load(open('config.json'))
for k in ('left_channel','gain_db','stereo','sample_rate','warmup_seconds','format','max_retries','retry_silence_fraction'):
    if k in c: print(f"  {k:24s} = {c[k]}")
PY
else
  echo "  (sin config.json, se usan defaults)"
fi

LEFT=$(python3 -c "import json;print('true' if json.load(open('config.json')).get('left_channel',False) else 'false')" 2>/dev/null || echo false)
CH_CFG=$([ "$LEFT" = true ] && echo left || echo right)
CH_OPP=$([ "$LEFT" = true ] && echo right || echo left)
WARMUP=$(python3 -c "import json;print(json.load(open('config.json')).get('warmup_seconds',4.0))" 2>/dev/null || echo 4.0)

# ---------------------------------------------------------------------------
rec() { # rec <tag> <duracion> <titulo> [args app...]
  local tag="$1" dur="$2" title="$3"; shift 3
  local wav="$OUT/debug_${TS}_${tag}.wav"
  local log="$OUT/debug_${TS}_${tag}.log"
  echo; echo "############ $title ############"
  echo "  -> grabando ${dur}s a $wav"
  echo -n "  -> HAGA RUIDO/HABLE en: "
  for i in 3 2 1; do echo -n "$i.. "; sleep 1; done
  printf '\a'; echo "YA!"
  sudo "$APP" --wav "$wav" --stereo -d "$dur" --gain 0 "$@" 2>&1 \
    | tee "$log" \
    | grep -E "L/R select|re-initialis|recording|warm|DROP|recover|dropouts|CLIPPING|ERROR|failed" || true
}

TS=$(date +%Y%m%d%H%M%S)

if [ "$NORECORD" = 0 ]; then
  echo; echo "########## INICIANDO PRUEBAS ##########"
  rec A "$SHORT_DUR" "TEST A: GPIO21 ON, canal $CH_CFG"       --warmup 0
  rec B "$SHORT_DUR" "TEST B: GPIO21 SIN tocar (--no-lr-gpio)" --warmup 0 --no-lr-gpio
  rec C "$SHORT_DUR" "TEST C: GPIO21 ON, canal $CH_OPP (opuesto)" --warmup 0 --channel "$CH_OPP"
  rec D "$SHORT_DUR" "TEST D: warmup normal (despertar del mic)"  --warmup "$WARMUP"
  rec E "$LONG_DUR"  "TEST E: grabacion larga (dropouts)"     --warmup 0
else
  TS=""
  for f in "$OUT"/debug_*_A.wav; do
    [ -e "$f" ] || continue
    b=$(basename "$f"); ts=${b#debug_}; ts=${ts%%_*}
    [[ "$ts" > "$TS" ]] && TS="$ts"
  done
  [ -n "$TS" ] || { echo "ERROR: no hay corridas previas para analizar (-n)."; exit 1; }
  echo "Analizando corrida $TS (sin grabar)"
fi

# ---------------------------------------------------------------------------
echo; echo "############ ANALISIS POR SLOT (dBFS, nivel bruto, dropouts) ############"
CH_CFG="$CH_CFG" CH_OPP="$CH_OPP" DROP_THRESH="$DROP_THRESH" TS="$TS" python3 - "$OUT" <<'PYEOF'
import sys, os, re, math, wave, struct

OUT = sys.argv[1]
TS = os.environ.get('TS', '????')
CH_CFG = os.environ.get('CH_CFG', '?')
CH_OPP = os.environ.get('CH_OPP', '?')
THRESH = float(os.environ.get('DROP_THRESH', '0.25'))

def db(v, full=32768.0):
    return -120.0 if v <= 0 else 20.0*math.log10(v/full)

def ch_stats(samples, rate):
    n = len(samples)
    peak = max((abs(x) for x in samples), default=0)
    rms = math.sqrt(sum(x*x for x in samples)/n) if n else 0.0
    ini = 0
    while ini < n and samples[ini] == 0:
        ini += 1
    th = max(1, int(THRESH*rate))
    events = []
    i = 0
    while i < n:
        if samples[i] == 0:
            j = i
            while j < n and samples[j] == 0:
                j += 1
            if j - i >= th:
                events.append((i, j - i))
            i = j
        else:
            i += 1
    zeros = sum(1 for x in samples if x == 0)
    return dict(peak=peak, rms=rms, ini=ini, events=events,
                zpercent=100.0*zeros/n if n else 0.0)

def load(p):
    if not os.path.exists(p):
        return None
    try:
        w = wave.open(p, 'rb')
    except Exception:
        return None
    n, ch, sw, fr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    data = w.readframes(n); w.close()
    if ch != 2 or sw != 2:
        return None
    s = struct.unpack('<%dh' % (2*n), data)
    return dict(fr=fr, n=n, dur=n/float(fr),
                L=ch_stats(s[0::2], fr), R=ch_stats(s[1::2], fr))

def fmt_ev(ch, fr):
    if not ch['events']:
        return '-'
    tot = sum(e[1] for e in ch['events'])
    mx = max(e[1] for e in ch['events'])
    return f"{len(ch['events'])} ev, {tot/fr:.1f}s, max {mx/fr:.1f}s"

def desc(ch, fr):
    return (f"pico {db(ch['peak']):6.1f}  rms {db(ch['rms']):6.1f}  "
            f"ceros {ch['zpercent']:5.1f}%  silIni {ch['ini']/fr:4.1f}s  |  {fmt_ev(ch, fr)}")

def alive(ch):
    return ch['peak'] > 32768*0.001

def strong_slot(a):
    if a is None:
        return 'none'
    L, R = a['L'], a['R']
    if alive(L) and not alive(R): return 'left'
    if alive(R) and not alive(L): return 'right'
    if alive(L) and alive(R):     return 'left' if L['peak'] >= R['peak'] else 'right'
    return 'none'

def active_ch(a):
    if a is None:
        return None, a
    return (a['R'] if a['R']['peak'] >= a['L']['peak'] else a['L']), a

tests = [('A', 'TEST A (GPIO on, canal %s)' % CH_CFG),
         ('B', 'TEST B (sin GPIO21)'),
         ('C', 'TEST C (GPIO on, canal %s)' % CH_OPP),
         ('D', 'TEST D (warmup normal)'),
         ('E', 'TEST E (grabacion larga)')]
data = {}
for tag, name in tests:
    f = f"debug_{TS}_{tag}.wav"
    a = load(os.path.join(OUT, f))
    data[tag] = a
    print(); print(f"== {name} ==")
    if a is None:
        print(f"  {f:36s} (no existe o formato inesperado)")
        continue
    L, R = a['L'], a['R']
    strong = 'L' if L['peak'] >= R['peak'] else 'R'
    print(f"  {f:36s} {a['dur']:5.1f}s  fuerte={strong}")
    print(f"    L: {desc(L, a['fr'])}")
    print(f"    R: {desc(R, a['fr'])}")

sA = strong_slot(data['A'])
sB = strong_slot(data['B'])
sC = strong_slot(data['C'])

print(); print("############ RESULTADO ############")
real = next((v for v in (sA, sB, sC) if v in ('left', 'right')), 'none')

if real == 'none':
    print("  El mic NO transmite senal viva en ninguna prueba.")
    print("  -> Revisar alimentacion 3V3, GND y los pines SD / BCLK / WS.")
    print("     Un cable SD flojo se lee como todo ceros (silencio digital).")
    print("     Agregar 0.1uF entre VDD y GND justo en el microfono.")
else:
    print(f"  Slot real del mic: {real.upper()}")
    if sA == 'none' and sB in ('left', 'right'):
        print("  -> El driver GPIO21 MATA al microfono: con GPIO ON no hay senal,")
        print("     con --no-lr-gpio SI la hay. Revisar el cableado de GPIO21")
        print("     (pin 40) o usar --no-lr-gpio con el canal correcto.")
    elif real == CH_CFG:
        print(f"  Config de canal OK: left_channel={'false' if real == 'right' else 'true'}")
        print("  coincide con el slot real del microfono.")
    else:
        print(f"  DESALINEADO: la config lee '{CH_CFG}' pero el mic transmite en '{real}'.")
        print(f"  -> Corregir en config.json: left_channel={'true' if real == 'left' else 'false'}")
        print(f"     (o usar -c {real}).")

if sA in ('left', 'right') and sB in ('left', 'right'):
    if sA == sB:
        print(f"  GPIO21 no cambia el slot (A={sA}, B={sB}) -> el pin L/R no esta")
        print("  cableado o el mic lo tiene fijo; no afecta la config.")
    else:
        print(f"  GPIO21 SI cambia el slot (A={sA}, B={sB}) -> pin L/R cableado.")

print(); print("  ----- Dropouts (apagones de senal, canal activo) -----")
worst_z = 0.0
worst_max = 0.0
for tag in ('A', 'E'):
    a = data[tag]
    if a is None:
        continue
    ch, a2 = active_ch(a)
    if ch is None:
        continue
    tot = sum(e[1] for e in ch['events'])
    mx = max((e[1] for e in ch['events']), default=0)
    worst_z = max(worst_z, ch['zpercent'])
    worst_max = max(worst_max, mx/float(a['fr']))
    pos = ''
    if ch['events']:
        i = max(range(len(ch['events'])), key=lambda k: ch['events'][k][1])
        frac = ch['events'][i][0]/float(a['n'])
        pos = ' (inicio)' if frac < 0.25 else (' (mitad)' if frac < 0.75 else ' (final)')
    print(f"  {tag:10s}: {len(ch['events'])} evento(s), {tot/a['fr']:6.1f}s de {a['dur']:.1f}s   "
          f"max {mx/a['fr']:4.1f}s{pos}   ceros {ch['zpercent']:.1f}%")

print()
print(f"  Ceros en el canal activo (peor prueba): {worst_z:.1f}%   apagon maximo: {worst_max:.1f}s")
if worst_z < 1.0:
    print("  -> Senal limpia en estas pruebas (sin dropouts relevantes).")
elif worst_z < 10.0:
    print("  -> Dropouts MODERADOS: revisar conexiones SD/BCLK/WS y agregar 0.1uF")
    print("     entre VDD y GND en el microfono; probar otro cable.")
else:
    print("  -> Dropouts SEVEROS: el mic se apaga solo (hardware). Revisar")
    print("     3V3 solido, GND comun, 0.1uF en VDD, cables SD/BCLK/WS, o")
    print("     reemplazar el microfono. El auto-retry del app (max_retries)")
    print("     mitiga, pero no reemplaza el fix fisico.")

lp = os.path.join(OUT, f"debug_{TS}_D.log")
if os.path.exists(lp):
    t1 = t2 = None
    alive_s = None
    for line in open(lp, errors='replace'):
        m = re.match(r'(\d\d):(\d\d):(\d\d)', line)
        if not m:
            continue
        t = int(m.group(1))*3600 + int(m.group(2))*60 + int(m.group(3))
        if 'warming up' in line:
            t1 = t
        elif 'warm-up done' in line:
            t2 = t
            m2 = re.search(r'\(([\d.]+) s of live audio', line)
            if m2:
                alive_s = float(m2.group(1))
    if t1 is not None and t2 is not None:
        dt = (t2 - t1) % 86400
        if alive_s is not None and alive_s <= 0.1 and dt >= 29.0:
            print(f"  Despertar del mic: NO desperto en {dt:.0f}s (cap 30s) - revisar alimentacion.")
        else:
            print(f"  Despertar del mic: espero {dt:.1f}s hasta lograr {alive_s:.1f}s de audio vivo.")

print()
print(f"  Archivos de esta corrida: {OUT}/debug_{TS}_*.wav  (borrar con: ./scripts/diag_mic.sh -c)")
PYEOF
echo
echo "== fin del diagnostico =="
