#ifndef PC_SIGNAL_HANDLER_H
#define PC_SIGNAL_HANDLER_H

#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pc {

class SignalHandler {
public:
    static void install();
    static bool is_stop_requested();
    static void reset();

private:
    static std::atomic<bool> stop_requested_;
#ifdef _WIN32
    static BOOL WINAPI console_handler(DWORD dw_type);
#else
    static void signal_callback(int signum);
#endif
};

} // namespace pc

#endif // PC_SIGNAL_HANDLER_H
