// Shared replication wire framing implementation.

#include "network/replication_wire.h"

#include "core/error.h"

#include <limits>
#include <string>

namespace snt::network::detail {
namespace {

constexpr uint32_t kWireMagic = 0x534E5452u;  // "SNTR"
constexpr size_t kMaxControlPayloadBytes = 64;

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

void append_u16(std::vector<std::byte>& bytes, uint16_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::byte>(value & 0xffu));
}

void append_u32(std::vector<std::byte>& bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::byte>& bytes, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

[[nodiscard]] uint16_t read_u16(std::span<const std::byte> bytes, size_t offset) {
    return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset])) << 8u |
           static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset + 1]));
}

[[nodiscard]] uint32_t read_u32(std::span<const std::byte> bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] uint64_t read_u64(std::span<const std::byte> bytes, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] bool known_kind(WirePacketKind kind) noexcept {
    switch (kind) {
        case WirePacketKind::Application:
        case WirePacketKind::TcpHello:
        case WirePacketKind::TcpHelloAck:
        case WirePacketKind::UdpAssociate:
        case WirePacketKind::UdpAssociateAck:
            return true;
    }
    return false;
}

[[nodiscard]] bool known_channel(ReplicationChannel channel) noexcept {
    return channel == ReplicationChannel::Reliable || channel == ReplicationChannel::Unreliable;
}

[[nodiscard]] snt::core::Expected<void> validate_packet_shape(
    WirePacketKind kind, ReplicationChannel channel, size_t payload_size,
    const ReplicationTransportConfig& config) {
    if (config.protocol_version == 0) {
        return protocol_error("Replication protocol version must be non-zero");
    }
    if (!known_kind(kind)) return protocol_error("Unknown replication wire packet kind");
    if (!known_channel(channel)) return protocol_error("Unknown replication wire channel");

    if (kind == WirePacketKind::Application) {
        const size_t maximum = channel == ReplicationChannel::Reliable
            ? config.max_reliable_payload_bytes
            : config.max_unreliable_payload_bytes;
        if (payload_size > maximum) {
            return protocol_error("Replication application payload exceeds configured limit");
        }
        return {};
    }

    const bool tcp_control = kind == WirePacketKind::TcpHello ||
                             kind == WirePacketKind::TcpHelloAck;
    const ReplicationChannel expected_channel = tcp_control
        ? ReplicationChannel::Reliable
        : ReplicationChannel::Unreliable;
    if (channel != expected_channel) {
        return protocol_error("Replication control packet was sent on the wrong channel");
    }
    if (payload_size > kMaxControlPayloadBytes) {
        return protocol_error("Replication control payload exceeds configured limit");
    }
    return {};
}

}  // namespace

snt::core::Expected<std::optional<size_t>> try_wire_packet_size(
    std::span<const std::byte> bytes, const ReplicationTransportConfig& config) {
    if (bytes.size() < kWireHeaderBytes) return std::optional<size_t>{};

    if (read_u32(bytes, 0) != kWireMagic) {
        return protocol_error("Replication wire magic does not match");
    }
    if (read_u16(bytes, 4) != config.protocol_version) {
        return protocol_error("Replication protocol version does not match");
    }

    const auto kind = static_cast<WirePacketKind>(std::to_integer<uint8_t>(bytes[6]));
    const auto channel = static_cast<ReplicationChannel>(std::to_integer<uint8_t>(bytes[7]));
    const size_t payload_size = read_u32(bytes, 16);
    if (auto result = validate_packet_shape(kind, channel, payload_size, config); !result) {
        return result.error();
    }

    if (payload_size > std::numeric_limits<size_t>::max() - kWireHeaderBytes) {
        return protocol_error("Replication wire packet size overflow");
    }
    const size_t packet_size = kWireHeaderBytes + payload_size;
    if (bytes.size() < packet_size) return std::optional<size_t>{};
    return std::optional<size_t>{packet_size};
}

snt::core::Expected<std::vector<std::byte>> encode_wire_packet(
    const WirePacket& packet, const ReplicationTransportConfig& config) {
    if (packet.payload.size() > std::numeric_limits<uint32_t>::max()) {
        return protocol_error("Replication wire payload cannot be represented in 32 bits");
    }
    if (auto result = validate_packet_shape(packet.kind, packet.channel, packet.payload.size(), config);
        !result) {
        return result.error();
    }

    std::vector<std::byte> bytes;
    bytes.reserve(kWireHeaderBytes + packet.payload.size());
    append_u32(bytes, kWireMagic);
    append_u16(bytes, config.protocol_version);
    bytes.push_back(static_cast<std::byte>(packet.kind));
    bytes.push_back(static_cast<std::byte>(packet.channel));
    append_u64(bytes, packet.server_tick);
    append_u32(bytes, static_cast<uint32_t>(packet.payload.size()));
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
    return bytes;
}

snt::core::Expected<WirePacket> decode_wire_packet(
    std::span<const std::byte> bytes, const ReplicationTransportConfig& config) {
    const auto packet_size = try_wire_packet_size(bytes, config);
    if (!packet_size) return packet_size.error();
    if (!*packet_size) return protocol_error("Replication wire packet is incomplete");
    if (**packet_size != bytes.size()) {
        return protocol_error("Replication wire packet has trailing bytes");
    }

    WirePacket packet;
    packet.kind = static_cast<WirePacketKind>(std::to_integer<uint8_t>(bytes[6]));
    packet.channel = static_cast<ReplicationChannel>(std::to_integer<uint8_t>(bytes[7]));
    packet.server_tick = read_u64(bytes, 8);
    packet.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kWireHeaderBytes), bytes.end());
    return packet;
}

}  // namespace snt::network::detail
