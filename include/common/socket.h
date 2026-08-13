#ifndef PC_SOCKET_H
#define PC_SOCKET_H

#include <string>
#include <cstdint>
#include <vector>
#include "common/types.h"

#ifndef _WIN32
#include <cstddef>
#endif

namespace pc {

#ifndef _WIN32
using ssize_t = ssize_t;
#else
using ssize_t = int;
#endif

class Socket {
public:
    explicit Socket(Transport transport = Transport::TCP);
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    void bind(const std::string& address, uint16_t port);
    void listen(int backlog = 5);
    Socket accept();
    void connect(const std::string& address, uint16_t port);

    ssize_t send_data(const uint8_t* data, size_t len);
    ssize_t recv_data(uint8_t* buffer, size_t maxlen);
    void set_recv_timeout(int milliseconds);

    ssize_t send_to(const std::string& address, uint16_t port,
                    const uint8_t* data, size_t len);
    ssize_t recv_from(std::string& address, uint16_t& port,
                      uint8_t* buffer, size_t maxlen);

    void close();
    bool is_open() const;

private:
    class Impl;
    Impl* impl_;
};

// ── Frame helpers (length-prefixed) ──────────────────────────────

void send_frame(Socket& sock, const std::string& json);
std::string recv_frame(Socket& sock);

void send_frame_udp(Socket& sock, const std::string& address, uint16_t port,
                    const std::string& json);
std::string recv_frame_udp(Socket& sock, std::string& from_address,
                           uint16_t& from_port);

} // namespace pc

#endif // PC_SOCKET_H
