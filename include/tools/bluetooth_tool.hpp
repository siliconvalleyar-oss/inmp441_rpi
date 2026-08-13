//////////////////////////////////////////////////////////////////
//
//                  bluetooth_tool.hpp
//
// Descripción: Define la clase `BluetoothTool`, que gestiona la
//              conexión de audio por Bluetooth A2DP usando la
//              herramienta del sistema `bluetoothctl` y deja el
//              sink de PulseAudio listo como salida por defecto.
//
//              El flujo típico es:
//                1. Encender el adaptador BT del Raspberry Pi.
//                2. Emparejar (si hace falta) con PIN "0000".
//                3. Conectar el perfil A2DP.
//                4. Indicar a PulseAudio que use ese dispositivo
//                   como salida por defecto.
//
//              La reproducción de audio la hace `Player` a través
//              de libao; este módulo solo se encarga de dejar
//              "listo" el dispositivo de salida.
//
// Autor: lion
// Fecha: 2024 Octb (refactorizado)
//
//////////////////////////////////////////////////////////////////

#pragma once

#include <string>

namespace BLUETOOTH {

// Configuración Bluetooth.
struct BluetoothConfig {
    // Dirección MAC del altavoz (p. ej. "AA:BB:CC:DD:EE:FF").
    std::string mac;
    // PIN de emparejamiento si el dispositivo lo solicita.
    std::string pin = "0000";
};

// Gestor de conexión Bluetooth A2DP mediante bluetoothctl.
class BluetoothTool {
public:
    BluetoothTool() = default;
    ~BluetoothTool() = default;

    BluetoothTool(const BluetoothTool&) = delete;
    BluetoothTool& operator=(const BluetoothTool&) = delete;

    // Encender BT, emparejar/conectar y dejar el dispositivo como
    // salida por defecto. Devuelve true si quedó conectado.
    bool connect(const std::string& mac);

    // Igual que connect() pero usando una configuración completa.
    bool connect(const BluetoothConfig& config);

    // Desconecta el dispositivo indicado.
    bool disconnect(const std::string& mac);

    // Limpia el soft-block rfkill del Bluetooth (sysfs). Lo usa connect()
    // para que el adaptador pueda encenderse incluso si systemd-rfkill o
    // algún servicio lo volvió a bloquear tras el arranque.
    void unblockRfkill();

    // Comprueba si el dispositivo está conectado (perfil A2DP).
    bool isConnected(const std::string& mac);

    // Si hay un sink Bluetooth en PulseAudio, lo deja por defecto.
    void setDefaultSink(const std::string& mac);

    // Última MAC usada en connect().
    const std::string& lastDevice() const { return lastMac_; }

    // PulseAudio es por-usuario y rechaza a root (Access denied): cuando la
    // app corre con sudo (make run, necesario para /dev/mem al grabar), esta
    // función detecta la sesión del usuario real (/run/user/*/pulse/native) y
    // hace seteuid/setegid a ese usuario, de forma que pactl y libao vean el
    // sink Bluetooth. Devuelve true si se dropearon privilegios (el llamador
    // debe restaurarlos con restorePulseUser() antes de salir). No-op si ya
    // corre como el usuario normal (devuelve false).
    static bool dropToPulseUser();

    // Restaura root (seteuid/setegid a 0) tras dropToPulseUser().
    static void restorePulseUser();

private:
    // Ejecuta un comando del sistema y devuelve su código de salida.
    static int run(const std::string& cmd);

    // Ejecuta un comando y captura su salida estándar.
    static std::string capture(const std::string& cmd);

    // Empareja el dispositivo si no está ya emparejado.
    void pair(const std::string& mac, const std::string& pin);

    std::string lastMac_;
};

} // namespace BLUETOOTH
