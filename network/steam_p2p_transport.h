// Steam P2P replication adapter contract.
//
// The engine does not link Steamworks. A platform integration implements
// ISteamP2PBackend with SteamNetworkingSockets and injects it here. This
// adapter owns the same SNT wire codec and IReplicationTransport lifecycle as
// TCP+UDP, making game replication independent of Steam headers and handles.

#pragma once

#include "network/replication.h"

#include <memory>
#include <span>

namespace snt::network {

using SteamPeerId = uint64_t;

enum class SteamP2PBackendEventKind : uint8_t {
    PeerConnected,
    PeerDisconnected,
    PacketReceived,
};

struct SteamP2PBackendEvent {
    SteamP2PBackendEventKind kind = SteamP2PBackendEventKind::PeerConnected;
    SteamPeerId peer = 0;
    ReplicationChannel channel = ReplicationChannel::Reliable;
    std::vector<std::byte> bytes;
    std::string detail;
};

class ISteamP2PBackend {
public:
    virtual ~ISteamP2PBackend() = default;

    virtual snt::core::Expected<void> send(
        SteamPeerId peer, ReplicationChannel channel,
        std::span<const std::byte> bytes) = 0;
    virtual snt::core::Expected<void> disconnect(
        SteamPeerId peer, std::string_view reason) = 0;
    virtual snt::core::Expected<std::vector<SteamP2PBackendEvent>> poll() = 0;
};

class SteamP2PReplicationTransport final : public IReplicationTransport {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<SteamP2PReplicationTransport>> create(
        ISteamP2PBackend& backend, ReplicationTransportConfig config = {});

    ~SteamP2PReplicationTransport() override;

    SteamP2PReplicationTransport(const SteamP2PReplicationTransport&) = delete;
    SteamP2PReplicationTransport& operator=(const SteamP2PReplicationTransport&) = delete;

    [[nodiscard]] snt::core::Expected<void> send(PeerId peer,
                                                   const ReplicationFrame& frame) override;
    [[nodiscard]] snt::core::Expected<void> disconnect(PeerId peer,
                                                         std::string_view reason) override;
    [[nodiscard]] snt::core::Expected<std::vector<ReplicationEvent>> poll() override;
    [[nodiscard]] std::vector<PeerId> connected_peers() const override;
    void shutdown() noexcept override;

private:
    SteamP2PReplicationTransport(ISteamP2PBackend& backend,
                                 ReplicationTransportConfig config);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::network
