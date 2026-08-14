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
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/Logger.hpp"

namespace BLUETOOTH {

namespace {
core::Logger& log() { return core::Logger::instance(); }

// UID/GID del usuario al que se dropearon los privilegios (0 = sin dropear).
uid_t g_droppedUid = 0;
gid_t g_droppedGid = 0;
}  // namespace

bool BluetoothTool::dropToPulseUser() {
    // Si ya corremos como el usuario normal, el entorno es correcto.
    if (::geteuid() != 0) {
        return false;
    }

    // Como root: buscar el primer servidor PulseAudio del sistema
    // (/run/user/<uid>/pulse/native). PulseAudio comprueba el uid REAL del
    // proceso (getuid()), así que seteuid() no basta: hay que poner el uid
    // real y el effective al usuario (setresuid) dejando el saved uid en 0
    // para poder volver a root con restorePulseUser().
    DIR* dir = ::opendir("/run/user");
    if (dir == nullptr) {
        return false;
    }

    struct dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        const std::string runtime = std::string("/run/user/") + entry->d_name;
        const std::string socket = runtime + "/pulse/native";
        struct stat st {};
        if (::stat(socket.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)) {
            const uid_t uid = static_cast<uid_t>(std::strtoul(entry->d_name, nullptr, 10));
            if (uid == 0) {
                continue;
            }
            g_droppedUid = uid;
            g_droppedGid = st.st_gid;
            ::setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1);
            // Al bajar de root, HOME sigue siendo /root, pero libpulse
            // (y por tanto libao) autentica contra PulseAudio con la cookie
            // de $HOME/.config/pulse/cookie: sin ella el handshake falla y
            // el audio "se reproduce" sin llegar a ningún sink. Apuntar HOME
            // al home real del usuario resuelve la autenticación.
            struct passwd* pw = ::getpwuid(uid);
            if (pw != nullptr && pw->pw_dir != nullptr && *pw->pw_dir != '\0') {
                ::setenv("HOME", pw->pw_dir, 1);
            }
            // Orden obligatorio: primero el grupo, luego el usuario.
            // setresuid(uid, uid, 0): real y effective al usuario (PulseAudio
            // los verifica), saved en 0 para poder restaurar root después.
            if (::setresgid(g_droppedGid, g_droppedGid, 0) != 0 ||
                ::setresuid(g_droppedUid, g_droppedUid, 0) != 0) {
                g_droppedUid = 0;
                g_droppedGid = 0;
                ::closedir(dir);
                return false;
            }
            log().info("Bluetooth: using PulseAudio session of user '%s'",
                       entry->d_name);
            ::closedir(dir);
            return true;
        }
    }
    ::closedir(dir);
    return false;
}

void BluetoothTool::restorePulseUser() {
    if (g_droppedUid == 0) {
        return;
    }
    // El saved uid quedó en 0, así que root es restaurable: primero el
    // effective (recupera capabilities) y luego real+saved completos.
    ::seteuid(0);
    ::setegid(0);
    ::setresuid(0, 0, 0);
    ::setresgid(0, 0, 0);
    g_droppedUid = 0;
    g_droppedGid = 0;
}

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
    // systemd-rfkill (o un servicio) vuelve a bloquear el adaptador tras el
    // arranque: limpiar el soft-block antes de encenderlo evita el fallo
    // "adapter-not-powered" que hacía imposible conectar el altavoz.
    unblockRfkill();
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

    if (!isConnected(config.mac)) {
        log().warning("Bluetooth: could not connect to %s. Make sure the "
                      "speaker is powered on and in pairing mode.", config.mac.c_str());
        log().warning("Bluetooth device state:\n%s",
                      capture("timeout 5 bluetoothctl info " + config.mac + " 2>&1 | head -8").c_str());
        log().warning("PulseAudio sinks:\n%s",
                      capture("timeout 5 pactl list short sinks 2>&1 | head -10").c_str());
        return false;
    }

    // Política estricta: el audio solo sale por este dispositivo. Verifica
    // que PulseAudio tenga el sink de ESTA MAC como por defecto; si no lo
    // tiene, se aborta en vez de caer al altavoz local.
    std::string macUnderscored = config.mac;
    for (char& c : macUnderscored) {
        if (c == ':') c = '_';
    }
    // pactl get-default-sink no existe en PulseAudio 14 (Bullseye): la
    // forma portable de leer el sink por defecto es `pactl info`.
    const std::string sinkOk = capture(
        "pactl info 2>/dev/null | grep -i 'Default Sink' | grep -qi '" +
        macUnderscored + "' && echo yes");
    if (sinkOk.find("yes") == std::string::npos) {
        log().warning("Bluetooth: PulseAudio has no default sink for %s; "
                      "audio will NOT be routed. Reconnect the speaker and "
                      "retry.", config.mac.c_str());
        log().warning("PulseAudio sinks:\n%s",
                      capture("timeout 5 pactl list short sinks 2>&1 | head -10").c_str());
        return false;
    }

    lastMac_ = config.mac;
    log().info("Bluetooth: connected to %s (A2DP) - audio via its sink.",
               config.mac.c_str());
    return true;
}

bool BluetoothTool::disconnect(const std::string& mac) {
    log().info("Bluetooth: disconnecting %s ...", mac.c_str());
    return run("timeout 5 bluetoothctl disconnect " + mac + " >/dev/null 2>&1") == 0;
}

void BluetoothTool::unblockRfkill() {
    // sysfs escribible solo por root; sin sudo (y tras dropToPulseUser)
    // no se puede, por eso se llama antes de bajar privilegios.
    if (::geteuid() != 0) {
        return;
    }
    DIR* dir = ::opendir("/sys/class/rfkill");
    if (dir == nullptr) {
        return;
    }
    while (struct dirent* entry = ::readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string base = std::string("/sys/class/rfkill/") + entry->d_name;

        FILE* typeFile = ::fopen((base + "/type").c_str(), "r");
        if (typeFile == nullptr) continue;
        char type[32] = {};
        const bool isBt = ::fscanf(typeFile, "%31s", type) == 1 &&
                          std::string(type) == "bluetooth";
        ::fclose(typeFile);
        if (!isBt) continue;

        FILE* soft = ::fopen((base + "/soft").c_str(), "w");
        if (soft != nullptr) {
            std::fprintf(soft, "0");
            ::fclose(soft);
            log().info("Bluetooth: rfkill soft-block cleared (%s)", entry->d_name);
        }
    }
    ::closedir(dir);
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

    // Política estricta: solo se selecciona el sink de ESTA MAC (nunca un
    // sink bluez genérico ni el altavoz local).
    std::string cmd =
        "SINK=$(pactl list short sinks 2>/dev/null | grep -i '" + macUnderscored +
        "' | head -1 | awk '{print $2}'); "
        "[ -n \"$SINK\" ] && pactl set-default-sink \"$SINK\" >/dev/null 2>&1";

    // pactl es rápido y seguro (a diferencia de bluetoothctl, no se cuelga
    // sin TTY), así que no hace falta timeout aquí.
    run(cmd);
}

} // namespace BLUETOOTH
