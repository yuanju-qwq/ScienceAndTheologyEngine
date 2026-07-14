// Steam P2P replication adapter implementation.
//
// SteamNetworkingSockets integration belongs behind ISteamP2PBackend. This
// file owns only SNT replication framing and peer lifecycle translation, so
// neither the engine nor game protocol headers need Steamworks types.

#define SNT_LOG_CHANNEL "network.steam_p2p"
#include "network/steam_p2p_transport.h"

#include "network/replication_wire.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace snt::network {
namespace {

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

[[nodiscard]] snt::core::Expected<void> validate_config(const ReplicationTransportConfig& config) {
    if (config.protocol_version == 0 || config.max_reliable_payload_bytes == 0 ||
        config.max_unreliable_payload_bytes == 0) {
        return invalid_argument("Steam P2P replication config has an invalid protocol or payload limit");
    }
    if (config.max_reliable_payload_bytes > std::numeric_limits<uint32_t>::max() ||
        config.max_unreliable_payload_bytes > std::numeric_limits<uint32_t>::max()) {
        return invalid_argument("Steam P2P replication payload limits exceed the wire format");
    }
    return {};
}

}  // namespace

struct SteamP2PReplicationTransport::Impl {
    ISteamP2PBackend* backend = nullptr;
    ReplicationTransportConfig config;
    std::unordered_set<PeerId> peers;
    bool stopped = false;
    uint32_t suppressed_protocol_warnings = 0;
    std::chrono::steady_clock::time_point last_protocol_warning{};

    void warn_protocol(std::string_view message) {
        ++suppressed_protocol_warnings;
        const auto now = std::chrono::steady_clock::now();
        if (last_protocol_warning.time_since_epoch().count() != 0 &&
            now - last_protocol_warning < std::chrono::seconds(5)) {
            return;
        }
        SNT_LOG_WARN("Dropped %u invalid Steam P2P replication packet(s): %.*s",
                     suppressed_protocol_warnings, static_cast<int>(message.size()), message.data());
        suppressed_protocol_warnings = 0;
        last_protocol_warning = now;
    }

    void reject_peer(PeerId peer, std::string reason, std::vector<ReplicationEvent>& events) {
        const bool was_connected = peers.erase(peer) != 0;
        if (auto result = backend->disconnect(peer, reason); !result) {
            SNT_LOG_WARN("Steam P2P backend could not disconnect peer %llu: %s",
                         static_cast<unsigned long long>(peer), result.error().format().c_str());
        }
        if (was_connected) {
            events.push_back({
                .kind = ReplicationEventKind::PeerDisconnected,
                .peer = peer,
                .detail = std::move(reason),
            });
        }
    }
};

SteamP2PReplicationTransport::SteamP2PReplicationTransport(
    ISteamP2PBackend& backend, ReplicationTransportConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->backend = &backend;
    impl_->config = std::move(config);
}

SteamP2PReplicationTransport::~SteamP2PReplicationTransport() { shutdown(); }

snt::core::Expected<std::unique_ptr<SteamP2PReplicationTransport>>
SteamP2PReplicationTransport::create(
    ISteamP2PBackend& backend, ReplicationTransportConfig config) {
    if (auto result = validate_config(config); !result) return result.error();
    return std::unique_ptr<SteamP2PReplicationTransport>(
        new SteamP2PReplicationTransport(backend, std::move(config)));
}

snt::core::Expected<void> SteamP2PReplicationTransport::send(
    PeerId peer, const ReplicationFrame& frame) {
    if (!impl_ || impl_->stopped) return invalid_state("Steam P2P transport is shut down");
    if (peer == kInvalidPeerId || !impl_->peers.contains(peer)) {
        return invalid_state("Steam P2P replication frame targets an unconnected peer");
    }
    if (frame.protocol_version != impl_->config.protocol_version) {
        return protocol_error("Replication frame protocol version does not match Steam P2P transport");
    }

    detail::WirePacket packet;
    packet.kind = detail::WirePacketKind::Application;
    packet.channel = frame.channel;
    packet.server_tick = frame.server_tick;
    packet.payload = frame.payload;
    auto encoded = detail::encode_wire_packet(packet, impl_->config);
    if (!encoded) return encoded.error();
    if (auto result = impl_->backend->send(peer, frame.channel, *encoded); !result) {
        auto error = result.error();
        error.with_context("SteamP2PReplicationTransport::send(backend)");
        return error;
    }
    return {};
}

snt::core::Expected<void> SteamP2PReplicationTransport::disconnect(PeerId peer,
                                                                     std::string_view reason) {
    if (!impl_ || impl_->stopped) return invalid_state("Steam P2P transport is shut down");
    if (peer == kInvalidPeerId || !impl_->peers.contains(peer)) {
        return invalid_argument("Cannot disconnect an unknown Steam P2P peer");
    }
    if (auto result = impl_->backend->disconnect(peer, reason); !result) return result.error();
    impl_->peers.erase(peer);
    SNT_LOG_INFO("Steam P2P replication peer %llu disconnected locally: %.*s",
                 static_cast<unsigned long long>(peer), static_cast<int>(reason.size()), reason.data());
    return {};
}

snt::core::Expected<std::vector<ReplicationEvent>> SteamP2PReplicationTransport::poll() {
    if (!impl_ || impl_->stopped) return invalid_state("Steam P2P transport is shut down");
    auto backend_events = impl_->backend->poll();
    if (!backend_events) {
        auto error = backend_events.error();
        error.with_context("SteamP2PReplicationTransport::poll(backend)");
        return error;
    }

    std::vector<ReplicationEvent> events;
    for (const auto& backend_event : *backend_events) {
        const PeerId peer = backend_event.peer;
        if (peer == kInvalidPeerId) {
            impl_->warn_protocol("Steam backend emitted an invalid peer id");
            continue;
        }

        switch (backend_event.kind) {
            case SteamP2PBackendEventKind::PeerConnected:
                if (impl_->peers.insert(peer).second) {
                    events.push_back({.kind = ReplicationEventKind::PeerConnected, .peer = peer});
                    SNT_LOG_INFO("Steam P2P replication peer %llu connected",
                                 static_cast<unsigned long long>(peer));
                }
                break;
            case SteamP2PBackendEventKind::PeerDisconnected:
                if (impl_->peers.erase(peer) != 0) {
                    events.push_back({
                        .kind = ReplicationEventKind::PeerDisconnected,
                        .peer = peer,
                        .detail = backend_event.detail,
                    });
                }
                break;
            case SteamP2PBackendEventKind::PacketReceived: {
                if (!impl_->peers.contains(peer)) {
                    impl_->warn_protocol("Steam backend delivered a packet for an unconnected peer");
                    if (auto result = impl_->backend->disconnect(peer, "packet before connection"); !result) {
                        SNT_LOG_WARN("Steam P2P backend rejected unknown packet peer poorly: %s",
                                     result.error().format().c_str());
                    }
                    break;
                }
                const std::span<const std::byte> bytes(backend_event.bytes.data(),
                                                       backend_event.bytes.size());
                auto packet = detail::decode_wire_packet(bytes, impl_->config);
                if (!packet || packet->kind != detail::WirePacketKind::Application ||
                    packet->channel != backend_event.channel) {
                    const std::string reason = !packet
                        ? "Steam P2P protocol error: " + packet.error().format()
                        : "Steam P2P channel does not match encoded replication frame";
                    impl_->warn_protocol(reason);
                    impl_->reject_peer(peer, reason, events);
                    break;
                }
                events.push_back({
                    .kind = ReplicationEventKind::FrameReceived,
                    .peer = peer,
                    .frame = {
                        .protocol_version = impl_->config.protocol_version,
                        .server_tick = packet->server_tick,
                        .channel = packet->channel,
                        .payload = std::move(packet->payload),
                    },
                });
                break;
            }
        }
    }
    return events;
}

std::vector<PeerId> SteamP2PReplicationTransport::connected_peers() const {
    std::vector<PeerId> peers;
    if (!impl_ || impl_->stopped) return peers;
    peers.reserve(impl_->peers.size());
    for (const PeerId peer : impl_->peers) peers.push_back(peer);
    std::sort(peers.begin(), peers.end());
    return peers;
}

void SteamP2PReplicationTransport::shutdown() noexcept {
    if (!impl_ || impl_->stopped) return;
    impl_->stopped = true;
    for (const PeerId peer : impl_->peers) {
        if (auto result = impl_->backend->disconnect(peer, "transport shutdown"); !result) {
            SNT_LOG_WARN("Steam P2P backend shutdown disconnect failed for peer %llu: %s",
                         static_cast<unsigned long long>(peer), result.error().format().c_str());
        }
    }
    impl_->peers.clear();
    SNT_LOG_INFO("Steam P2P replication transport shut down");
}

}  // namespace snt::network
