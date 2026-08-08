#include "common/signal_handler.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#endif

namespace pc {

std::atomic<bool> SignalHandler::stop_requested_{false};

void SignalHandler::install() {
    reset();
#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    struct sigaction sa{};
    sa.sa_handler = signal_callback;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#endif
}

bool SignalHandler::is_stop_requested() {
    return stop_requested_.load();
}

void SignalHandler::reset() {
    stop_requested_.store(false);
}

#ifdef _WIN32
BOOL WINAPI SignalHandler::console_handler(DWORD dw_type) {
    if (dw_type == CTRL_C_EVENT || dw_type == CTRL_BREAK_EVENT ||
        dw_type == CTRL_CLOSE_EVENT || dw_type == CTRL_LOGOFF_EVENT ||
        dw_type == CTRL_SHUTDOWN_EVENT) {
        stop_requested_.store(true);
        return TRUE;
    }
    return FALSE;
}
#else
void SignalHandler::signal_callback(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        stop_requested_.store(true);
    }
}
#endif

} // namespace pc
