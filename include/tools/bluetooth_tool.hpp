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

    // Devuelve la MAC del primer dispositivo emparejado, o "" si no
    // hay ninguno. Útil cuando el usuario no ha configurado --bt-mac.
    std::string discoverPairedDevice() const;

    // Encender BT, emparejar/conectar y dejar el dispositivo como
    // salida por defecto. Devuelve true si quedó conectado.
    bool connect(const std::string& mac);

    // Igual que connect() pero usando una configuración completa.
    bool connect(const BluetoothConfig& config);

    // Desconecta el dispositivo indicado.
    bool disconnect(const std::string& mac);

    // Comprueba si el dispositivo está conectado (perfil A2DP).
    bool isConnected(const std::string& mac);

    // Si hay un sink Bluetooth en PulseAudio, lo deja por defecto.
    void setDefaultSink(const std::string& mac);

    // Última MAC usada en connect().
    const std::string& lastDevice() const { return lastMac_; }

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
