// Internal wire framing shared by direct sockets and Steam P2P adapters.
//
// The wire packet carries only transport-neutral replication frames. Game
// command/snapshot semantics remain opaque payload bytes owned by a handler.

#pragma once

#include "network/replication.h"

#include <optional>
#include <span>

namespace snt::network::detail {

enum class WirePacketKind : uint8_t {
    Application = 0,
    TcpHello = 1,
    TcpHelloAck = 2,
    UdpAssociate = 3,
    UdpAssociateAck = 4,
};

struct WirePacket {
    WirePacketKind kind = WirePacketKind::Application;
    ReplicationChannel channel = ReplicationChannel::Reliable;
    uint64_t server_tick = 0;
    std::vector<std::byte> payload;
};

inline constexpr size_t kWireHeaderBytes = 20;

// Returns nullopt while a TCP stream has not received a complete header or
// payload. A non-empty error means the peer supplied an invalid frame.
[[nodiscard]] snt::core::Expected<std::optional<size_t>> try_wire_packet_size(
    std::span<const std::byte> bytes,
    const ReplicationTransportConfig& config);

[[nodiscard]] snt::core::Expected<std::vector<std::byte>> encode_wire_packet(
    const WirePacket& packet,
    const ReplicationTransportConfig& config);

// `bytes` must contain exactly one complete packet. Call try_wire_packet_size
// first for TCP stream buffers.
[[nodiscard]] snt::core::Expected<WirePacket> decode_wire_packet(
    std::span<const std::byte> bytes,
    const ReplicationTransportConfig& config);

}  // namespace snt::network::detail
