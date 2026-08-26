#include "common/socket.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

namespace pc {

class Socket::Impl {
public:
#ifdef _WIN32
    SOCKET sock = INVALID_SOCKET;
#else
    int sock = -1;
#endif
    Transport transport;
    bool open = false;

    bool valid() const {
#ifdef _WIN32
        return sock != INVALID_SOCKET;
#else
        return sock >= 0;
#endif
    }
};

Socket::Socket(Transport transport)
    : impl_(new Impl{}) {
    impl_->transport = transport;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        throw std::runtime_error("WSAStartup failed");
    impl_->sock = ::socket(AF_INET,
        transport == Transport::TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (impl_->sock == INVALID_SOCKET)
        throw std::runtime_error("socket creation failed: " + std::to_string(WSAGetLastError()));
#else
    impl_->sock = ::socket(AF_INET,
        transport == Transport::TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (impl_->sock < 0)
        throw std::runtime_error(std::string("socket creation failed: ") + std::strerror(errno));
#endif
    impl_->open = true;
}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept
    : impl_(std::move(other.impl_)) {
    other.impl_ = nullptr;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
        other.impl_ = nullptr;
    }
    return *this;
}

void Socket::bind(const std::string& address, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1)
        throw std::runtime_error("Invalid address: " + address);

#ifndef _WIN32
    // Allow re-binding a port whose prior connection is still in
    // TIME_WAIT/FIN-WAIT (e.g. back-to-back test runs on the same port).
    int opt = 1;
    ::setsockopt(impl_->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

#ifdef _WIN32
    if (::bind(impl_->sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        throw std::runtime_error("bind failed: " + std::to_string(WSAGetLastError()));
#else
    if (::bind(impl_->sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));
#endif
}

void Socket::listen(int backlog) {
    if (impl_->transport != Transport::TCP)
        throw std::runtime_error("listen() requires TCP");
#ifdef _WIN32
    if (::listen(impl_->sock, backlog) == SOCKET_ERROR)
        throw std::runtime_error("listen failed");
#else
    if (::listen(impl_->sock, backlog) < 0)
        throw std::runtime_error("listen failed");
#endif
}

Socket Socket::accept() {
    if (impl_->transport != Transport::TCP)
        throw std::runtime_error("accept() requires TCP");
    sockaddr_in addr{};
    int addrlen = sizeof(addr);
#ifdef _WIN32
    SOCKET client = ::accept(impl_->sock, reinterpret_cast<sockaddr*>(&addr), &addrlen);
    if (client == INVALID_SOCKET)
        throw std::runtime_error("accept failed");
    Socket s(Transport::TCP);
    s.impl_->sock = client;
    s.impl_->open = true;
#else
    int client = ::accept(impl_->sock, reinterpret_cast<sockaddr*>(&addr), reinterpret_cast<socklen_t*>(&addrlen));
    if (client < 0)
        throw std::runtime_error("accept failed");
    Socket s(Transport::TCP);
    s.impl_->sock = client;
    s.impl_->open = true;
#endif
    return s;
}

void Socket::connect(const std::string& address, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1)
        throw std::runtime_error("Invalid address: " + address);

#ifdef _WIN32
    if (::connect(impl_->sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        throw std::runtime_error("connect failed: " + std::to_string(WSAGetLastError()));
#else
    if (::connect(impl_->sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error(std::string("connect failed: ") + std::strerror(errno));
#endif
    impl_->open = true;
}

ssize_t Socket::send_data(const uint8_t* data, size_t len) {
    if (!impl_->open) return -1;
#ifdef _WIN32
    int sent = ::send(impl_->sock, reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
    return sent == SOCKET_ERROR ? -1 : sent;
#else
    return ::send(impl_->sock, data, len, 0);
#endif
}

ssize_t Socket::recv_data(uint8_t* buffer, size_t maxlen) {
    if (!impl_->open) return -1;
#ifdef _WIN32
    int received = ::recv(impl_->sock, reinterpret_cast<char*>(buffer), static_cast<int>(maxlen), 0);
    if (received == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAETIMEDOUT) return -2;
        return -1;
    }
    return received;
#else
    int received = ::recv(impl_->sock, buffer, maxlen, 0);
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -2;
    return received;
#endif
}

void Socket::set_recv_timeout(int milliseconds) {
    if (!impl_->valid()) return;
#ifdef _WIN32
    int timeout = milliseconds;
    ::setsockopt(impl_->sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval tv;
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(impl_->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

ssize_t Socket::send_to(const std::string& address, uint16_t port,
                       const uint8_t* data, size_t len) {
    if (impl_->transport != Transport::UDP) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &addr.sin_addr);
#ifdef _WIN32
    int sent = ::sendto(impl_->sock, reinterpret_cast<const char*>(data),
                        static_cast<int>(len), 0,
                        reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return sent == SOCKET_ERROR ? -1 : sent;
#else
    return ::sendto(impl_->sock, data, len, 0,
                    reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#endif
}

ssize_t Socket::recv_from(std::string& address, uint16_t& port,
                         uint8_t* buffer, size_t maxlen) {
    if (impl_->transport != Transport::UDP) return -1;
    sockaddr_in addr{};
    int addrlen = sizeof(addr);
#ifdef _WIN32
    int received = ::recvfrom(impl_->sock, reinterpret_cast<char*>(buffer),
                              static_cast<int>(maxlen), 0,
                              reinterpret_cast<sockaddr*>(&addr), &addrlen);
    if (received == SOCKET_ERROR) return -1;
#else
    socklen_t addrlen_t = sizeof(addr);
    int received = ::recvfrom(impl_->sock, buffer, maxlen, 0,
                              reinterpret_cast<sockaddr*>(&addr), &addrlen_t);
    if (received < 0) return -1;
#endif
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    address = buf;
    port = ntohs(addr.sin_port);
    return received;
}

void Socket::close() {
    if (impl_ && impl_->open) {
#ifdef _WIN32
        if (impl_->valid()) ::closesocket(impl_->sock);
#else
        if (impl_->valid()) {
            // shutdown() reliably unblocks a blocked accept()/recv(); close() alone
            // does not on Linux.
            ::shutdown(impl_->sock, SHUT_RDWR);
            ::close(impl_->sock);
        }
#endif
        impl_->open = false;
    }
}

bool Socket::is_open() const {
    return impl_ && impl_->open;
}

// ── Frame helpers ────────────────────────────────────────────────

static void write_bytes(Socket& sock, const uint8_t* data, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        ssize_t n = sock.send_data(data + offset, len - offset);
        if (n < 0) throw std::runtime_error("send failed");
        offset += static_cast<size_t>(n);
    }
}

static size_t read_exact(Socket& sock, uint8_t* buffer, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        ssize_t n = sock.recv_data(buffer + offset, len - offset);
        if (n == -2) throw std::runtime_error("Socket recv timeout");
        if (n <= 0) return offset;
        offset += static_cast<size_t>(n);
    }
    return offset;
}

void send_frame(Socket& sock, const std::string& json) {
    uint32_t len = static_cast<uint32_t>(json.size());
    uint32_t net_len = htonl(len);
    write_bytes(sock, reinterpret_cast<const uint8_t*>(&net_len), 4);
    write_bytes(sock, reinterpret_cast<const uint8_t*>(json.data()), json.size());
}

std::string recv_frame(Socket& sock) {
    uint8_t header[4];
    if (read_exact(sock, header, 4) < 4) {
        throw std::runtime_error("Connection closed while reading frame header");
    }
    uint32_t net_len = *reinterpret_cast<uint32_t*>(header);
    uint32_t len = ntohl(net_len);
    std::string payload(len, '\0');
    if (read_exact(sock, reinterpret_cast<uint8_t*>(payload.data()), len) < len) {
        throw std::runtime_error("Connection closed while reading frame payload");
    }
    return payload;
}

void send_frame_udp(Socket& sock, const std::string& address, uint16_t port,
                    const std::string& json) {
    uint32_t len = static_cast<uint32_t>(json.size());
    uint32_t net_len = htonl(len);
    std::vector<uint8_t> frame(4 + json.size());
    std::memcpy(frame.data(), &net_len, 4);
    std::memcpy(frame.data() + 4, json.data(), json.size());
    sock.send_to(address, port, frame.data(), frame.size());
}

std::string recv_frame_udp(Socket& sock, std::string& from_address,
                           uint16_t& from_port) {
    uint8_t buf[65535];
    ssize_t n = sock.recv_from(from_address, from_port, buf, sizeof(buf));
    if (n < 4) return "";
    uint32_t net_len = *reinterpret_cast<uint32_t*>(buf);
    uint32_t len = ntohl(net_len);
    if (n < 4 + static_cast<ssize_t>(len)) return "";
    return std::string(reinterpret_cast<char*>(buf + 4), len);
}

} // namespace pc
