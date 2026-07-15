// Replication contracts -- transport-neutral authoritative networking boundary.
//
// Ownership: concrete transports own socket/platform state; a game-owned
// IReplicationHandler owns message semantics and main-thread World mutation.
// ReplicationService only orders transport events around a fixed simulation
// tick. It intentionally contains no Steam, SDL, Vulkan, or game payload type.
//
// Thread affinity: poll_inbound() and emit_outbound() are simulation-main-
// thread calls. Transports may use non-blocking OS I/O internally, but never
// mutate an ECS World or invoke a handler from an I/O thread.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/expected.h"

namespace snt::network {

using PeerId = uint64_t;
inline constexpr PeerId kInvalidPeerId = 0;
inline constexpr PeerId kServerPeerId = 1;
inline constexpr uint16_t kCurrentReplicationProtocolVersion = 1;

enum class ReplicationChannel : uint8_t {
    Reliable,
    Unreliable,
};

struct ReplicationFrame {
    uint16_t protocol_version = kCurrentReplicationProtocolVersion;
    uint64_t server_tick = 0;
    ReplicationChannel channel = ReplicationChannel::Reliable;
    std::vector<std::byte> payload;
};

struct ReplicationTransportConfig {
    uint16_t protocol_version = kCurrentReplicationProtocolVersion;
    size_t max_reliable_payload_bytes = 4u * 1024u * 1024u;
    size_t max_unreliable_payload_bytes = 1200u;
    // This must accommodate one maximum-sized reliable frame plus its wire
    // header. Five MiB leaves a small burst allowance without silently
    // making a configured 4 MiB payload impossible to send.
    size_t max_pending_reliable_bytes_per_peer = 5u * 1024u * 1024u;
    uint32_t max_peers = 64;
};

enum class ReplicationEventKind : uint8_t {
    PeerConnected,
    PeerDisconnected,
    FrameReceived,
};

// A transport event contains a frame only for FrameReceived. Disconnect
// detail is diagnostic text, not a user-visible localization key.
struct ReplicationEvent {
    ReplicationEventKind kind = ReplicationEventKind::PeerConnected;
    PeerId peer = kInvalidPeerId;
    ReplicationFrame frame;
    std::string detail;
};

class IReplicationTransport {
public:
    virtual ~IReplicationTransport() = default;

    virtual snt::core::Expected<void> send(PeerId peer,
                                           const ReplicationFrame& frame) = 0;
    // A locally requested disconnect does not synthesize a PeerDisconnected
    // event. The caller that requested it owns any immediate game cleanup;
    // poll() reports only transport-observed remote lifecycle changes.
    virtual snt::core::Expected<void> disconnect(PeerId peer,
                                                 std::string_view reason) = 0;
    virtual snt::core::Expected<std::vector<ReplicationEvent>> poll() = 0;
    virtual std::vector<PeerId> connected_peers() const = 0;
    virtual void shutdown() noexcept = 0;
};

struct ReplicationTickContext {
    uint64_t tick_index = 0;
    float delta_seconds = 0.0f;
};

class IReplicationFrameSink {
public:
    virtual ~IReplicationFrameSink() = default;

    virtual snt::core::Expected<void> send(PeerId peer,
                                           const ReplicationFrame& frame) = 0;
    virtual snt::core::Expected<void> broadcast(const ReplicationFrame& frame) = 0;
    // Handlers can request a lifecycle change only from their outbound phase.
    // The sink remains the sole owner of the concrete transport and handler
    // cleanup remains explicit because IReplicationTransport::disconnect()
    // does not synthesize a PeerDisconnected event.
    virtual snt::core::Expected<void> disconnect(PeerId peer,
                                                 std::string_view reason) = 0;
};

// Implemented by the game/server composition layer. A handler receives a
// frame only from ReplicationService::poll_inbound on the simulation main
// thread, so it may queue deterministic gameplay commands but must not retain
// frame references after returning.
class IReplicationHandler {
public:
    virtual ~IReplicationHandler() = default;

    virtual snt::core::Expected<void> on_peer_connected(
        PeerId peer, const ReplicationTickContext& context) = 0;
    virtual snt::core::Expected<void> on_frame(
        PeerId peer, const ReplicationFrame& frame,
        const ReplicationTickContext& context) = 0;
    virtual void on_peer_disconnected(PeerId peer, std::string_view reason) noexcept = 0;
    virtual snt::core::Expected<void> emit_outbound(
        const ReplicationTickContext& context, IReplicationFrameSink& sink) = 0;
};

// Applies inbound transport events before simulation systems run and gives the
// game one outbound emission point after the scheduler barrier. Handler
// validation failures disconnect only that peer; transport failures are
// returned to the runtime host as Expected errors.
class ReplicationService final : private IReplicationFrameSink {
public:
    ReplicationService(IReplicationTransport& transport, IReplicationHandler& handler);

    [[nodiscard]] snt::core::Expected<void> poll_inbound(
        const ReplicationTickContext& context);
    [[nodiscard]] snt::core::Expected<void> emit_outbound(
        const ReplicationTickContext& context);
    void shutdown() noexcept;

private:
    snt::core::Expected<void> send(PeerId peer,
                                   const ReplicationFrame& frame) override;
    snt::core::Expected<void> broadcast(const ReplicationFrame& frame) override;
    snt::core::Expected<void> disconnect(PeerId peer,
                                         std::string_view reason) override;

    IReplicationTransport* transport_ = nullptr;
    IReplicationHandler* handler_ = nullptr;
    bool shutdown_ = false;
};

}  // namespace snt::network
