#pragma once

#include <atomic>

namespace core {

// Installs SIGINT / SIGTERM handlers and exposes a cooperative shutdown flag
// so the capture loops can exit cleanly (flushing WAV headers, etc.).
class SignalHandler {
public:
    static void install();
    static bool shouldStop();

private:
    static void handleSignal(int signal);

    static std::atomic<bool> stopRequested_;
};

}  // namespace core
