#include <gtest/gtest.h>
#include "common/socket.h"
#include <thread>
#include <chrono>

using namespace pc;

namespace {

// Each test uses its own port to avoid conflicts.
constexpr uint16_t kTcpPort1 = 19901;
constexpr uint16_t kTcpPort2 = 19902;
constexpr uint16_t kTcpPort3 = 19903;
constexpr uint16_t kTcpPort4 = 19904;
constexpr uint16_t kUdpPort1 = 19910;

} // namespace

TEST(SocketFrame, TcpRoundTrip) {
    Socket server(Transport::TCP);
    server.bind("127.0.0.1", kTcpPort1);
    server.listen(1);

    Socket client(Transport::TCP);
    client.connect("127.0.0.1", kTcpPort1);

    Socket accepted = server.accept();

    std::string payload = R"({"msg_type":"test","value":42})";
    send_frame(client, payload);
    std::string received = recv_frame(accepted);

    EXPECT_EQ(received, payload);
}

TEST(SocketFrame, TcpMultipleFrames) {
    Socket server(Transport::TCP);
    server.bind("127.0.0.1", kTcpPort2);
    server.listen(1);

    Socket client(Transport::TCP);
    client.connect("127.0.0.1", kTcpPort2);

    Socket accepted = server.accept();

    for (int i = 0; i < 5; i++) {
        std::string payload = R"({"seq":)" + std::to_string(i) + "}";
        send_frame(client, payload);
    }

    for (int i = 0; i < 5; i++) {
        std::string received = recv_frame(accepted);
        EXPECT_EQ(received, R"({"seq":)" + std::to_string(i) + "}");
    }
}

TEST(SocketFrame, TcpBidirectional) {
    Socket server(Transport::TCP);
    server.bind("127.0.0.1", kTcpPort3);
    server.listen(1);

    Socket client(Transport::TCP);
    client.connect("127.0.0.1", kTcpPort3);

    Socket accepted = server.accept();

    send_frame(client, "from-client");
    send_frame(accepted, "from-server");

    EXPECT_EQ(recv_frame(accepted), "from-client");
    EXPECT_EQ(recv_frame(client), "from-server");
}

TEST(SocketFrame, TcpEmptyPayload) {
    Socket server(Transport::TCP);
    server.bind("127.0.0.1", kTcpPort4);
    server.listen(1);

    Socket client(Transport::TCP);
    client.connect("127.0.0.1", kTcpPort4);

    Socket accepted = server.accept();

    send_frame(client, "");
    std::string received = recv_frame(accepted);

    EXPECT_TRUE(received.empty());
}

TEST(SocketFrame, TcpRecvAfterClose_Throws) {
    Socket server(Transport::TCP);
    server.bind("127.0.0.1", kTcpPort4 + 1);
    server.listen(1);

    Socket client(Transport::TCP);
    client.connect("127.0.0.1", kTcpPort4 + 1);

    Socket accepted = server.accept();
    client.close();

    EXPECT_THROW(recv_frame(accepted), std::runtime_error);
}

TEST(SocketFrame, UdpRoundTrip) {
    Socket server(Transport::UDP);
    server.bind("127.0.0.1", kUdpPort1);
    server.set_recv_timeout(5000);

    Socket client(Transport::UDP);

    std::string payload = R"({"msg_type":"test","value":7})";
    send_frame_udp(client, "127.0.0.1", kUdpPort1, payload);

    std::string from_addr;
    uint16_t from_port = 0;
    std::string received = recv_frame_udp(server, from_addr, from_port);

    EXPECT_EQ(received, payload);
    EXPECT_EQ(from_addr, "127.0.0.1");
    EXPECT_GT(from_port, 0);
}

TEST(SocketFrame, UdpMultipleFrames) {
    Socket server(Transport::UDP);
    server.bind("127.0.0.1", kUdpPort1 + 1);
    server.set_recv_timeout(5000);

    Socket client(Transport::UDP);

    for (int i = 0; i < 3; i++) {
        send_frame_udp(client, "127.0.0.1", kUdpPort1 + 1,
                       R"({"seq":)" + std::to_string(i) + "}");
    }

    for (int i = 0; i < 3; i++) {
        std::string from_addr;
        uint16_t from_port = 0;
        std::string received = recv_frame_udp(server, from_addr, from_port);
        EXPECT_EQ(received, R"({"seq":)" + std::to_string(i) + "}");
    }
}
