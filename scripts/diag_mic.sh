#!/usr/bin/env bash
# ============================================================================
# diag_mic.sh - Diagnostico del microfono INMP441 (slot I2S, GPIO21, dropouts)
#
# Genera grabaciones de prueba y analiza AMBOS slots I2S (estereo) para
# responder tres preguntas:
#   1) En que slot transmite el mic realmente (left vs right)?
#   2) El driver GPIO21 (L/R select) cambia algo o es el problema?
#   3) Cuanto dura el silencio digital inicial (mic en arranque)?
#
# Los WAV se guardan en output/ como debug_<AAAAmmddHHMMSS>_<test>.wav
#
# Uso:  sudo ./scripts/diag_mic.sh        (desde la raiz del repo)
# Durante cada grabacion haga ruido cerca del microfono (hable/chasquee).
# ============================================================================
set -u
cd "$(dirname "$0")/.."   # raiz del repo (donde estan bin/ y config.json)

APP=${APP:-"./bin/inmp441_rpi"}
DUR=${DUR:-8}             # segundos por grabacion
OUT=output                 # carpeta de destino de los WAV
TS=$(date +%Y%m%d%H%M%S)
[ -x "$APP" ] || { echo "ERROR: no existe $APP (compile primero: make)"; exit 1; }
mkdir -p "$OUT"

echo "== config actual =="
if [ -f config.json ]; then
  python3 - <<'PY'
import json
c = json.load(open('config.json'))
for k in ('left_channel','gain_db','stereo','sample_rate','warmup_seconds','format'):
    if k in c: print(f"  {k:16s} = {c[k]}")
PY
else
  echo "  (sin config.json, uso defaults)"
fi

LEFT=$(python3 -c "import json;print('true' if json.load(open('config.json')).get('left_channel',False) else 'false')" 2>/dev/null || echo false)
CH_CFG=$([ "$LEFT" = true ] && echo left || echo right)
CH_OPP=$([ "$LEFT" = true ] && echo right || echo left)

# ---------------------------------------------------------------------------
rec() { # rec <etiqueta> <args...>
  local tag="$1"; shift
  local out="$OUT/debug_${TS}_${tag}.wav"
  echo; echo "  >> grabando ${DUR}s a $out ... HAGA RUIDO/HABLE ahora"
  sleep 2
  sudo "$APP" --wav "$out" --stereo -d "$DUR" --warmup 0 --gain 0 "$@" 2>&1 \
    | grep -E "L/R select|recording|DROP|recovered|dropouts|ERROR|saved" || true
}

echo; echo "############ TEST A: GPIO21 driver ON (canal config = $CH_CFG) ############"
rec A_gpio

echo; echo "############ TEST B: GPIO21 SIN tocar (--no-lr-gpio) ############"
rec B_nogpio --no-lr-gpio

echo; echo "############ TEST C: GPIO21 ON pero leyendo canal $CH_OPP (el opuesto) ############"
rec C_opposite --channel "$CH_OPP"

# ---------------------------------------------------------------------------
echo; echo "############ ANALISIS POR SLOT (dBFS, nivel bruto, gain 0) ############"
CH_CFG="$CH_CFG" CH_OPP="$CH_OPP" TS="$TS" python3 - "$OUT" <<'PYEOF'
import sys, wave, struct, os, math

CH_CFG = os.environ.get('CH_CFG', '?')
CH_OPP = os.environ.get('CH_OPP', '?')
TS = os.environ.get('TS', '????')

def db(v, full=32768):
    if v <= 0: return -120.0
    return 20.0*math.log10(v/full)

def stats(samples):
    peak = max((abs(x) for x in samples), default=0)
    rms = math.sqrt(sum(x*x for x in samples)/len(samples)) if samples else 0
    zeros = 0
    for x in samples:
        if x == 0: zeros += 1
        else: break
    return db(peak), db(rms), zeros

files = [f"debug_{TS}_A_gpio.wav", f"debug_{TS}_B_nogpio.wav", f"debug_{TS}_C_opposite.wav"]
for f in files:
    p = os.path.join(sys.argv[1], f)
    if not os.path.exists(p):
        print(f"  {f}: (no existe)"); continue
    w = wave.open(p, 'rb')
    n, ch, sw, fr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    data = w.readframes(n); w.close()
    if ch != 2 or sw != 2:
        print(f"  {f}: formato inesperado ch={ch} sw={sw}"); continue
    s = struct.unpack('<%dh' % (2*n), data)
    L, R = s[0::2], s[1::2]
    lp, lr, lz = stats(L); rp, rr, rz = stats(R)
    dur = n/fr
    strong = 'L' if lp >= rp else 'R'
    print(f"  {f:34s} dur={dur:5.1f}s  L: pico {lp:7.1f} rms {lr:7.1f} silIni {lz/fr:5.2f}s  "
          f"|  R: pico {rp:7.1f} rms {rr:7.1f} silIni {rz/fr:5.2f}s   -> fuerte: {strong}")
    # ceros totales por canal
    nzL = sum(1 for x in L if x == 0)/len(L)*100
    nzR = sum(1 for x in R if x == 0)/len(R)*100
    print(f"  {'':32s}  ceros: L {nzL:5.1f}%  R {nzR:5.1f}%")

def strong_slot(f):
    w = wave.open(os.path.join(sys.argv[1], f), 'rb')
    n, ch, sw, fr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    data = w.readframes(n); w.close()
    s = struct.unpack('<%dh' % (2*n), data)
    aL = max((abs(x) for x in s[0::2]), default=0)
    aR = max((abs(x) for x in s[1::2]), default=0)
    return aL, aR

def is_alive(v):  # arriba del piso de ruido (~-60 dBFS bruto)
    return v > 32768*0.001

print(); print("############ VEREDICTO ############")
try:
    aL, aR = strong_slot(files[0])
    bL, bR = strong_slot(files[1])
    cL, cR = strong_slot(files[2])
except Exception as e:
    print("  (no hay archivos para concluir)", e); sys.exit(0)

print(f"  A (GPIO on, canal cfg={CH_CFG}):  L {db(aL):.0f}dB  R {db(aR):.0f}dB")
print(f"  B (no-lr-gpio):                   L {db(bL):.0f}dB  R {db(bR):.0f}dB")
print(f"  C (GPIO on, canal {CH_OPP}):      L {db(cL):.0f}dB  R {db(cR):.0f}dB")

slotA = 'left'  if aL >= aR else 'right'
slotB = 'left'  if bL >= bR else 'right'
slotC = 'left'  if cL >= cR else 'right'

if not is_alive(max(aL, aR)) and is_alive(max(bL, bR)):
    print("  -> El GPIO21 driver MATA al microfono: con la pin a HIGH el slot")
    print("     configurado queda sordo, pero con --no-lr-gpio SI hay senal.")
    print("     REVISAR CABLEADO de GPIO21 (fisico, pin 40) o usar --no-lr-gpio.")
elif is_alive(max(aL, aR)):
    real = slotA
    if real != CH_CFG:
        print(f"  -> El microfono transmite REALMENTE en slot '{real}', pero la config")
        print(f"     lee '{CH_CFG}' (left_channel={str(not (real=='left')).lower()}).")
        print(f"     Correccion: poner left_channel={'true' if real=='left' else 'false'} en config.json")
        print(f"     (o usar -c {real}).")
    else:
        print(f"  -> Config correcta: el mic transmite en '{real}' como indica la config.")
else:
    print("  -> Ningun slot tiene senal en el TEST A. Si tampoco en B/C,")
    print("     revisar alimentacion 3V3, BCLK/WS/SCK y el pin SD del microfono.")

if slotA == slotB:
    print(f"  -> GPIO21 no cambia el slot (en ambos casos transmite '{slotA}').")
else:
    print(f"  -> GPIO21 SI cambia el slot (A='{slotA}', B='{slotB}') => el pin esta cableado")
    print("     a L/R del microfono; el log 'left untouched' se debe interpretar con cuidado.")
PYEOF
echo
echo "Archivos en $OUT/ (debug_${TS}_*.wav)"
