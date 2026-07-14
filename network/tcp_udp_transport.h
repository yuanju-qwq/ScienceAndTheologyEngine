// Direct TCP + UDP replication transport.
//
// TCP carries reliable ordered frames. UDP carries only explicitly unreliable
// frames after a TCP handshake associates the UDP endpoint with one PeerId.
// All methods are simulation-main-thread calls and use non-blocking sockets.

#pragma once

#include "network/replication.h"

#include <memory>
#include <string>

namespace snt::network {

struct TcpUdpListenConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t tcp_port = 23585;
    uint16_t udp_port = 23586;
    ReplicationTransportConfig replication;
};

struct TcpUdpConnectConfig {
    std::string host = "127.0.0.1";
    uint16_t tcp_port = 23585;
    uint16_t udp_port = 23586;
    ReplicationTransportConfig replication;
};

class TcpUdpReplicationTransport final : public IReplicationTransport {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<TcpUdpReplicationTransport>> listen(
        TcpUdpListenConfig config);
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<TcpUdpReplicationTransport>> connect(
        TcpUdpConnectConfig config);

    ~TcpUdpReplicationTransport() override;

    TcpUdpReplicationTransport(const TcpUdpReplicationTransport&) = delete;
    TcpUdpReplicationTransport& operator=(const TcpUdpReplicationTransport&) = delete;

    [[nodiscard]] snt::core::Expected<void> send(PeerId peer,
                                                   const ReplicationFrame& frame) override;
    [[nodiscard]] snt::core::Expected<void> disconnect(PeerId peer,
                                                         std::string_view reason) override;
    [[nodiscard]] snt::core::Expected<std::vector<ReplicationEvent>> poll() override;
    [[nodiscard]] std::vector<PeerId> connected_peers() const override;
    void shutdown() noexcept override;

    [[nodiscard]] bool is_server() const noexcept;
    [[nodiscard]] bool is_unreliable_ready(PeerId peer) const noexcept;
    [[nodiscard]] uint16_t tcp_port() const noexcept;
    [[nodiscard]] uint16_t udp_port() const noexcept;

private:
    struct Impl;

    TcpUdpReplicationTransport();
    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::network
