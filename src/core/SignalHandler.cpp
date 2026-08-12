#include "core/SignalHandler.hpp"

#include <csignal>

namespace core {

std::atomic<bool> SignalHandler::stopRequested_{false};

void SignalHandler::install() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
}

bool SignalHandler::shouldStop() {
    return stopRequested_.load();
}

void SignalHandler::handleSignal(int) {
    stopRequested_.store(true);
}

}  // namespace core
