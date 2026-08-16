#!/usr/bin/env bash
# bt-connect.sh — Encender adaptador Bluetooth y conectar un speaker A2DP.
#
# Uso:
#   ./scripts/bt-connect.sh AA:BB:CC:DD:EE:FF
#   ./scripts/bt-connect.sh AA:BB:CC:DD:EE:FF 0000   # PIN explícito
#
# Soluciona el error "br-connection-adapter-not-powered" haciendo:
#   1. Limpia soft-block de rfkill (systemd-rfkill lo vuelve a bloquear al boot)
#   2. Enciende el adaptador con bluetoothctl power on
#   3. Empareja si hace falta (PIN por defecto 0000)
#   4. Conecta perfil A2DP
#   5. Setea PulseAudio default-sink al sink BT

set -euo pipefail

MAC="${1:-}"
PIN="${2:-0000}"

if [[ -z "$MAC" ]]; then
    echo "Uso: $0 <MAC> [PIN]"
    echo "Ejemplo: $0 AC:EF:92:D0:B5:BB"
    exit 1
fi

# ── 1. Limpiar rfkill soft-block ──────────────────────────────────
echo "[1/5] Limpiando rfkill soft-block..."
for dir in /sys/class/rfkill/rfkill*; do
    [[ -f "$dir/type" ]] || continue
    tipo=$(cat "$dir/type" 2>/dev/null || true)
    [[ "$tipo" == "bluetooth" ]] || continue
    echo 0 > "$dir/soft" 2>/dev/null && echo "  ✓ $(basename "$dir") desbloqueado"
done

# ── 2. Encender adaptador ────────────────────────────────────────
echo "[2/5] Encendiendo adaptador Bluetooth..."
timeout 10 bluetoothctl power on >/dev/null 2>&1
sleep 1

# Verificar que encendió
if ! timeout 5 bluetoothctl show 2>/dev/null | grep -q "Powered: yes"; then
    echo "  ✗ Adaptador no encendió. Revisa que exista un adaptador BT."
    echo "    bluetoothctl show"
    exit 1
fi
echo "  ✓ Adaptador encendido"

# ── 3. Asegurar PulseAudio ───────────────────────────────────────
echo "[3/5] Verificando PulseAudio..."
if ! timeout 5 pactl info >/dev/null 2>&1; then
    pulseaudio --start 2>/dev/null || true
    sleep 1
fi
if timeout 5 pactl info >/dev/null 2>&1; then
    echo "  ✓ PulseAudio activo"
else
    echo "  ⚠ PulseAudio no responde (audio BT no funcionará hasta que arranque)"
fi

# ── 4. Emparejar si hace falta ───────────────────────────────────
echo "[4/5] Verificando emparejamiento con $MAC..."
paired=$(timeout 5 bluetoothctl info "$MAC" 2>/dev/null | grep -c "Paired: yes" || true)

if [[ "$paired" -eq 0 ]]; then
    echo "  Emparejando (PIN: $PIN)..."
    timeout 10 bluetoothctl agent NoInputNoOutput >/dev/null 2>&1 || true
    timeout 10 bluetoothctl default-agent >/dev/null 2>&1 || true
    timeout 10 bluetoothctl trust "$MAC" >/dev/null 2>&1 || true
    timeout 15 bluetoothctl pair "$MAC" >/dev/null 2>&1 || true

    # Reintentar con agente KeyboardDisplay si el primero falló
    timeout 10 bluetoothctl agent KeyboardDisplay >/dev/null 2>&1 || true
    timeout 15 bluetoothctl pair "$MAC" >/dev/null 2>&1 || true

    paired=$(timeout 5 bluetoothctl info "$MAC" 2>/dev/null | grep -c "Paired: yes" || true)
    if [[ "$paired" -eq 0 ]]; then
        echo "  ✗ No se pudo emparejar. Verifica que el speaker esté en modo pairing."
        exit 1
    fi
    echo "  ✓ Emparejado"
else
    echo "  ✓ Ya emparejado"
    timeout 10 bluetoothctl trust "$MAC" >/dev/null 2>&1 || true
fi

# ── 5. Conectar A2DP ─────────────────────────────────────────────
echo "[5/5] Conectando A2DP a $MAC..."
timeout 15 bluetoothctl connect "$MAC" >/dev/null 2>&1 || true
sleep 3

if ! timeout 5 bluetoothctl info "$MAC" 2>/dev/null | grep -q "Connected: yes"; then
    echo "  ✗ No se pudo conectar. Estado:"
    timeout 5 bluetoothctl info "$MAC" 2>/dev/null | head -8
    echo "  Sinks disponibles:"
    timeout 5 pactl list short sinks 2>/dev/null | head -5
    exit 1
fi
echo "  ✓ Conectado"

# ── 6. Setear sink PulseAudio ────────────────────────────────────
mac_underscored="${MAC//:/_}"
sink=$(timeout 5 pactl list short sinks 2>/dev/null | grep -i "$mac_underscored" | head -1 | awk '{print $2}')
if [[ -n "$sink" ]]; then
    pactl set-default-sink "$sink" 2>/dev/null
    echo "  ✓ PulseAudio default-sink → $sink"
else
    echo "  ⚠ No se encontró sink PulseAudio para $MAC"
    echo "    Sinks: $(timeout 5 pactl list short sinks 2>/dev/null | head -3)"
fi

echo ""
echo "════════════════════════════════════════"
echo "  Speaker BT listo: $MAC"
echo "  audio sale por: $sink"
echo "════════════════════════════════════════"
