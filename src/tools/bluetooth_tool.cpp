//////////////////////////////////////////////////////////////////
//
//                  bluetooth_tool.cpp
//
// Descripción: Implementación de `BluetoothTool` usando
//              `bluetoothctl` de forma no interactiva.
//
//              Para el emparejamiento se usa el agente de
//              bluetoothctl con "0000" como PIN: para la mayoría
//              de los altavoces A2DP, el agente
//              `NoInputNoOutput` responde automáticamente "0000".
//
// Autor: lion
// Fecha: 2024 Octb (refactorizado)
//
//////////////////////////////////////////////////////////////////

#include "bluetooth_tool.hpp"

#include <cstdio>
#include <cstdlib>

#include "core/Logger.hpp"

namespace BLUETOOTH {

namespace {
core::Logger& log() { return core::Logger::instance(); }
}  // namespace

int BluetoothTool::run(const std::string& cmd) {
    return std::system(cmd.c_str());
}

std::string BluetoothTool::capture(const std::string& cmd) {
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) return out;
    char buf[128];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out += buf;
    }
    pclose(pipe);
    return out;
}

std::string BluetoothTool::discoverPairedDevice() const {
    const std::string out = capture(
        "timeout 5 bluetoothctl paired-devices 2>/dev/null | head -1 | awk '{print $2}'");
    std::string mac = out;
    // Recorta espacios / salto de línea.
    const size_t first = mac.find_first_not_of(" \t\r\n");
    const size_t last = mac.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    mac = mac.substr(first, last - first + 1);

    // La MAC debe tener el formato xx:xx:xx:xx:xx:xx para ser útil.
    if (mac.size() == 17 && mac.find(':') != std::string::npos) {
        return mac;
    }
    return std::string();
}

void BluetoothTool::pair(const std::string& mac, const std::string& pin) {
    // Sin TTY, bluetoothctl puede bloquearse esperando entrada: cada
    // llamada va envuelta en timeout para que nunca cuelgue la app.
    // Activar el agente por defecto: si el dispositivo pide PIN y es
    // "0000", el agente NoInputNoOutput lo responde automáticamente.
    run("timeout 10 bluetoothctl agent NoInputNoOutput >/dev/null 2>&1");
    run("timeout 10 bluetoothctl default-agent >/dev/null 2>&1");
    run("timeout 10 bluetoothctl trust " + mac + " >/dev/null 2>&1");

    std::string cmd = "timeout 15 bluetoothctl pair " + mac + " >/dev/null 2>&1";
    run(cmd);

    // Si el agente por defecto no bastó, reintentar con el agente que
    // muestra/teclea el PIN "0000" por nosotros.
    cmd = "timeout 10 bluetoothctl agent KeyboardDisplay >/dev/null 2>&1; "
          "timeout 15 bluetoothctl pair " + mac + " >/dev/null 2>&1";
    run(cmd);

    (void)pin; // El PIN "0000" lo maneja el agente; se documenta por claridad.
}

bool BluetoothTool::connect(const std::string& mac) {
    return connect(BluetoothConfig{mac});
}

bool BluetoothTool::connect(const BluetoothConfig& config) {
    log().info("Bluetooth: powering on adapter...");
    run("timeout 10 bluetoothctl power on >/dev/null 2>&1");

    // Asegurarse de que PulseAudio esté corriendo (necesario para el
    // sink de audio A2DP).
    run("timeout 5 pactl info >/dev/null 2>&1 || pulseaudio --start >/dev/null 2>&1");

    // Atajo: si ya está emparejado, no repetir el emparejamiento (cada
    // llamada a bluetoothctl sin TTY puede tardar/colgarse).
    const std::string paired = capture(
        "timeout 5 bluetoothctl info " + config.mac +
        " 2>/dev/null | grep -q \"Paired: yes\" && echo yes");
    if (paired.find("yes") == std::string::npos) {
        log().info("Bluetooth: pairing/trusting %s ...", config.mac.c_str());
        pair(config.mac, config.pin);
    } else {
        log().info("Bluetooth: %s already paired; trusting...", config.mac.c_str());
        run("timeout 10 bluetoothctl trust " + config.mac + " >/dev/null 2>&1");
    }

    log().info("Bluetooth: connecting A2DP to %s ...", config.mac.c_str());
    run("timeout 15 bluetoothctl connect " + config.mac + " >/dev/null 2>&1");

    // Dar tiempo a que PulseAudio descubra el sink del dispositivo.
    run("sleep 3");

    setDefaultSink(config.mac);

    if (isConnected(config.mac)) {
        lastMac_ = config.mac;
        log().info("Bluetooth: connected to %s (A2DP).", config.mac.c_str());
        return true;
    }

    log().warning("Bluetooth: could not connect to %s. Make sure the "
                  "speaker is powered on and in pairing mode.", config.mac.c_str());
    log().warning("Bluetooth device state:\n%s",
                  capture("timeout 5 bluetoothctl info " + config.mac + " 2>&1 | head -8").c_str());
    log().warning("PulseAudio sinks:\n%s",
                  capture("timeout 5 pactl list short sinks 2>&1 | head -10").c_str());
    return false;
}

bool BluetoothTool::disconnect(const std::string& mac) {
    log().info("Bluetooth: disconnecting %s ...", mac.c_str());
    return run("timeout 5 bluetoothctl disconnect " + mac + " >/dev/null 2>&1") == 0;
}

bool BluetoothTool::isConnected(const std::string& mac) {
    const std::string cmd =
        "timeout 5 bluetoothctl info " + mac + " | grep -q \"Connected: yes\"";
    return run(cmd) == 0;
}

void BluetoothTool::setDefaultSink(const std::string& mac) {
    // PulseAudio nombra los sinks Bluetooth con la MAC usando guiones
    // bajos en vez de ':' (p. ej. ac_ef_92_d0_b5_bb).
    std::string macUnderscored = mac;
    for (char& c : macUnderscored) {
        if (c == ':') c = '_';
    }

    // 1) Buscar un sink que contenga la MAC del dispositivo.
    std::string cmd =
        "SINK=$(pactl list short sinks 2>/dev/null | grep -i '" + macUnderscored +
        "' | head -1 | awk '{print $2}'); "
        "[ -n \"$SINK\" ] && pactl set-default-sink \"$SINK\" >/dev/null 2>&1";

    // 2) Si no se encontró, usar cualquier sink bluez existente.
    cmd += " || { "
           "SINK=$(pactl list short sinks 2>/dev/null | grep -i bluez | head -1 | awk '{print $2}'); "
           "[ -n \"$SINK\" ] && pactl set-default-sink \"$SINK\" >/dev/null 2>&1; }";

    // pactl es rápido y seguro (a diferencia de bluetoothctl, no se cuelga
    // sin TTY), así que no hace falta timeout aquí.
    run(cmd);
}

} // namespace BLUETOOTH
