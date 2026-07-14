// Direct TCP+UDP replication transport implementation.
//
// TCP owns ordered reliable frames. UDP is intentionally opt-in and becomes
// usable only after a TCP-issued association token binds the datagram endpoint
// to an established PeerId. The implementation is main-thread polled: it does
// not create socket threads and never reaches into ECS or game code.

#define SNT_LOG_CHANNEL "network.tcp_udp"
#include "network/tcp_udp_transport.h"

#include "network/replication_wire.h"
#include "network/socket_platform.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace snt::network {
namespace {

constexpr size_t kAssociationTokenBytes = 16;
constexpr size_t kTcpReceiveChunkBytes = 16u * 1024u;
constexpr size_t kMaxUdpDatagramBytes = 65507u;
constexpr size_t kUdpReceiveBufferBytes = 65535u;
constexpr size_t kMaxAcceptsPerPoll = 64;
constexpr size_t kMaxUdpDatagramsPerPoll = 128;
constexpr auto kUdpAssociateRetry = std::chrono::milliseconds(500);
constexpr auto kInvalidUdpWarningInterval = std::chrono::seconds(5);

using AssociationToken = std::array<std::byte, kAssociationTokenBytes>;

struct Ipv4Endpoint {
    sockaddr_in address{};
};

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] snt::core::Error network_error(std::string operation, int error) {
    return {snt::core::ErrorCode::kNetworkIoFailed,
            std::move(operation) + ": " + detail::socket_error_message(error)};
}

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

[[nodiscard]] bool same_endpoint(const Ipv4Endpoint& lhs, const Ipv4Endpoint& rhs) noexcept {
    return lhs.address.sin_family == rhs.address.sin_family &&
           lhs.address.sin_port == rhs.address.sin_port &&
           lhs.address.sin_addr.s_addr == rhs.address.sin_addr.s_addr;
}

[[nodiscard]] snt::core::Expected<Ipv4Endpoint> resolve_ipv4(
    const std::string& host, uint16_t port, bool passive) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = passive ? AI_PASSIVE : 0;

    const std::string service = std::to_string(port);
    addrinfo* results = nullptr;
    const char* host_value = host.empty() ? nullptr : host.c_str();
    const int result = getaddrinfo(host_value, service.c_str(), &hints, &results);
    if (result != 0 || results == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kNetworkInitFailed,
                                "IPv4 address resolution failed for '" + host + "': " +
                                    gai_strerror(result)};
    }

    Ipv4Endpoint endpoint;
    endpoint.address = *reinterpret_cast<const sockaddr_in*>(results->ai_addr);
    freeaddrinfo(results);
    return endpoint;
}

[[nodiscard]] snt::core::Expected<uint16_t> bound_port(detail::Socket socket) {
    sockaddr_in address{};
    detail::SocketLength length = sizeof(address);
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == detail::kSocketError) {
        return network_error("getsockname failed", detail::last_socket_error());
    }
    return ntohs(address.sin_port);
}

[[nodiscard]] AssociationToken generate_association_token() {
    std::random_device entropy;
    std::mt19937_64 generator(
        (static_cast<uint64_t>(entropy()) << 32u) ^ static_cast<uint64_t>(entropy()));
    AssociationToken token{};
    for (std::byte& value : token) {
        value = static_cast<std::byte>(generator() & 0xffu);
    }
    return token;
}

[[nodiscard]] bool token_equals(std::span<const std::byte> bytes,
                                const AssociationToken& token) noexcept {
    return bytes.size() == token.size() &&
           std::equal(bytes.begin(), bytes.end(), token.begin());
}

[[nodiscard]] snt::core::Expected<void> validate_replication_config(
    const ReplicationTransportConfig& config) {
    if (config.protocol_version == 0) {
        return invalid_argument("Replication protocol version must be non-zero");
    }
    if (config.max_reliable_payload_bytes == 0 || config.max_unreliable_payload_bytes == 0) {
        return invalid_argument("Replication payload limits must be non-zero");
    }
    if (config.max_reliable_payload_bytes > std::numeric_limits<uint32_t>::max() ||
        config.max_unreliable_payload_bytes > std::numeric_limits<uint32_t>::max()) {
        return invalid_argument("Replication payload limits must fit the 32-bit wire header");
    }
    if (config.max_unreliable_payload_bytes > kMaxUdpDatagramBytes - detail::kWireHeaderBytes) {
        return invalid_argument("Unreliable payload limit exceeds the largest IPv4 UDP datagram");
    }
    const size_t required_queue_bytes = detail::kWireHeaderBytes +
        std::max(config.max_reliable_payload_bytes, size_t{64});
    if (config.max_pending_reliable_bytes_per_peer < required_queue_bytes) {
        return invalid_argument(
            "Reliable send queue limit cannot hold one maximum replication frame");
    }
    if (config.max_peers == 0) return invalid_argument("Replication max_peers must be non-zero");
    return {};
}

[[nodiscard]] snt::core::Expected<void> configure_tcp_socket(detail::Socket socket) {
    if (!detail::set_socket_non_blocking(socket)) {
        return network_error("Could not set TCP socket non-blocking", detail::last_socket_error());
    }
    if (!detail::set_socket_tcp_no_delay(socket)) {
        return network_error("Could not disable TCP Nagle buffering", detail::last_socket_error());
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> configure_udp_socket(detail::Socket socket) {
    if (!detail::set_socket_non_blocking(socket)) {
        return network_error("Could not set UDP socket non-blocking", detail::last_socket_error());
    }
    return {};
}

}  // namespace

struct TcpUdpReplicationTransport::Impl {
    struct Peer {
        PeerId id = kInvalidPeerId;
        detail::Socket tcp_socket = detail::kInvalidSocket;
        bool connecting = false;
        bool tcp_ready = false;
        bool client_hello_sent = false;
        bool awaiting_hello_ack = false;
        bool has_server_token = false;
        bool udp_ready = false;
        AssociationToken client_token{};
        AssociationToken server_token{};
        std::optional<Ipv4Endpoint> udp_endpoint;
        std::chrono::steady_clock::time_point last_udp_associate{};
        std::vector<std::byte> receive_buffer;
        std::deque<std::vector<std::byte>> reliable_send_queue;
        size_t reliable_send_offset = 0;
        size_t pending_reliable_bytes = 0;
    };

    bool server = false;
    bool socket_platform_initialized = false;
    bool stopped = false;
    ReplicationTransportConfig config;
    detail::Socket tcp_listener = detail::kInvalidSocket;
    detail::Socket udp_socket = detail::kInvalidSocket;
    uint16_t bound_tcp_port = 0;
    uint16_t bound_udp_port = 0;
    std::optional<Ipv4Endpoint> remote_udp_endpoint;
    std::unordered_map<PeerId, Peer> peers;
    PeerId next_peer_id = kServerPeerId + 1;
    std::chrono::steady_clock::time_point last_invalid_udp_warning{};
    uint32_t suppressed_invalid_udp_packets = 0;
    std::chrono::steady_clock::time_point last_peer_limit_warning{};
    uint32_t suppressed_peer_limit_rejections = 0;

    ~Impl() { close_all(); }

    void close_peer(Peer& peer) noexcept {
        detail::close_socket(peer.tcp_socket);
        peer.tcp_socket = detail::kInvalidSocket;
        peer.receive_buffer.clear();
        peer.reliable_send_queue.clear();
        peer.pending_reliable_bytes = 0;
        peer.reliable_send_offset = 0;
    }

    void close_all() noexcept {
        for (auto& [_, peer] : peers) close_peer(peer);
        peers.clear();
        detail::close_socket(tcp_listener);
        detail::close_socket(udp_socket);
        tcp_listener = detail::kInvalidSocket;
        udp_socket = detail::kInvalidSocket;
        if (socket_platform_initialized) {
            detail::shutdown_socket_platform();
            socket_platform_initialized = false;
        }
    }

    [[nodiscard]] Peer* find_peer(PeerId peer_id) {
        const auto iterator = peers.find(peer_id);
        return iterator == peers.end() ? nullptr : &iterator->second;
    }

    [[nodiscard]] const Peer* find_peer(PeerId peer_id) const {
        const auto iterator = peers.find(peer_id);
        return iterator == peers.end() ? nullptr : &iterator->second;
    }

    [[nodiscard]] Peer* find_peer_by_udp_endpoint(const Ipv4Endpoint& endpoint) {
        for (auto& [_, peer] : peers) {
            if (peer.udp_endpoint && same_endpoint(*peer.udp_endpoint, endpoint)) return &peer;
        }
        return nullptr;
    }

    void warn_invalid_udp(std::string_view detail_message) {
        ++suppressed_invalid_udp_packets;
        const auto now = std::chrono::steady_clock::now();
        if (last_invalid_udp_warning.time_since_epoch().count() != 0 &&
            now - last_invalid_udp_warning < kInvalidUdpWarningInterval) {
            return;
        }
        SNT_LOG_WARN("Ignoring %u invalid UDP replication packet(s): %.*s",
                     suppressed_invalid_udp_packets,
                     static_cast<int>(detail_message.size()), detail_message.data());
        suppressed_invalid_udp_packets = 0;
        last_invalid_udp_warning = now;
    }

    void warn_peer_limit_reached() {
        ++suppressed_peer_limit_rejections;
        const auto now = std::chrono::steady_clock::now();
        if (last_peer_limit_warning.time_since_epoch().count() != 0 &&
            now - last_peer_limit_warning < kInvalidUdpWarningInterval) {
            return;
        }
        SNT_LOG_WARN("Rejected %u TCP replication connection(s): max_peers=%u is full",
                     suppressed_peer_limit_rejections, config.max_peers);
        suppressed_peer_limit_rejections = 0;
        last_peer_limit_warning = now;
    }

    void disconnect_peer(PeerId peer_id, std::string detail_message,
                         std::vector<ReplicationEvent>* events) {
        auto iterator = peers.find(peer_id);
        if (iterator == peers.end()) return;
        const bool was_connected = iterator->second.tcp_ready;
        close_peer(iterator->second);
        peers.erase(iterator);
        if (was_connected && events != nullptr) {
            events->push_back({
                .kind = ReplicationEventKind::PeerDisconnected,
                .peer = peer_id,
                .detail = std::move(detail_message),
            });
        }
    }

    [[nodiscard]] snt::core::Expected<void> queue_tcp_packet(
        Peer& peer, const detail::WirePacket& packet) {
        auto encoded = detail::encode_wire_packet(packet, config);
        if (!encoded) return encoded.error();
        if (encoded->size() > config.max_pending_reliable_bytes_per_peer -
                                  peer.pending_reliable_bytes) {
            return snt::core::Error{snt::core::ErrorCode::kNetworkIoFailed,
                                    "Reliable replication send queue limit exceeded"};
        }
        peer.pending_reliable_bytes += encoded->size();
        peer.reliable_send_queue.push_back(std::move(*encoded));
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> flush_tcp(Peer& peer) {
        while (!peer.reliable_send_queue.empty()) {
            const auto& packet = peer.reliable_send_queue.front();
            const size_t remaining = packet.size() - peer.reliable_send_offset;
            const int sent = ::send(peer.tcp_socket,
                                    reinterpret_cast<const char*>(packet.data() +
                                                                  peer.reliable_send_offset),
                                    static_cast<int>(remaining), 0);
            if (sent > 0) {
                const size_t sent_bytes = static_cast<size_t>(sent);
                peer.reliable_send_offset += sent_bytes;
                peer.pending_reliable_bytes -= sent_bytes;
                if (peer.reliable_send_offset == packet.size()) {
                    peer.reliable_send_queue.pop_front();
                    peer.reliable_send_offset = 0;
                }
                continue;
            }
            if (sent == 0) return network_error("TCP send returned zero", 0);

            const int error = detail::last_socket_error();
            if (detail::socket_would_block(error)) return {};
            return network_error("TCP send failed", error);
        }
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> send_udp_packet(
        const Ipv4Endpoint& endpoint, const detail::WirePacket& packet) {
        auto encoded = detail::encode_wire_packet(packet, config);
        if (!encoded) return encoded.error();
        const int sent = ::sendto(udp_socket, reinterpret_cast<const char*>(encoded->data()),
                                  static_cast<int>(encoded->size()), 0,
                                  reinterpret_cast<const sockaddr*>(&endpoint.address),
                                  sizeof(endpoint.address));
        if (sent == static_cast<int>(encoded->size())) return {};
        if (sent == detail::kSocketError) {
            const int error = detail::last_socket_error();
            // An unreliable packet is allowed to be dropped when the kernel
            // send buffer is full. The client retries association packets.
            if (detail::socket_would_block(error)) return {};
            return network_error("UDP sendto failed", error);
        }
        return snt::core::Error{snt::core::ErrorCode::kNetworkIoFailed,
                                "UDP sendto reported a partial datagram"};
    }

    [[nodiscard]] snt::core::Expected<void> start_client_hello(Peer& peer) {
        peer.client_token = generate_association_token();
        detail::WirePacket packet;
        packet.kind = detail::WirePacketKind::TcpHello;
        packet.channel = ReplicationChannel::Reliable;
        packet.payload.assign(peer.client_token.begin(), peer.client_token.end());
        if (auto result = queue_tcp_packet(peer, packet); !result) return result.error();
        peer.client_hello_sent = true;
        peer.awaiting_hello_ack = true;
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> maybe_send_udp_association(Peer& peer,
                                                                         bool force = false) {
        if (server || !peer.tcp_ready || peer.udp_ready || !peer.has_server_token ||
            !remote_udp_endpoint) {
            return {};
        }
        const auto now = std::chrono::steady_clock::now();
        if (!force && peer.last_udp_associate.time_since_epoch().count() != 0 &&
            now - peer.last_udp_associate < kUdpAssociateRetry) {
            return {};
        }

        detail::WirePacket packet;
        packet.kind = detail::WirePacketKind::UdpAssociate;
        packet.channel = ReplicationChannel::Unreliable;
        packet.payload.assign(peer.server_token.begin(), peer.server_token.end());
        peer.last_udp_associate = now;
        return send_udp_packet(*remote_udp_endpoint, packet);
    }

    [[nodiscard]] snt::core::Expected<void> finish_connect(Peer& peer) {
        fd_set write_set;
        fd_set error_set;
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        FD_SET(peer.tcp_socket, &write_set);
        FD_SET(peer.tcp_socket, &error_set);
        timeval timeout{};
#if defined(_WIN32)
        const int ready = select(0, nullptr, &write_set, &error_set, &timeout);
#else
        const int ready = select(peer.tcp_socket + 1, nullptr, &write_set, &error_set, &timeout);
#endif
        if (ready < 0) return network_error("TCP connect poll failed", detail::last_socket_error());
        if (ready == 0) return {};

        int connect_error = 0;
        detail::SocketLength error_length = sizeof(connect_error);
#if defined(_WIN32)
        const int result = getsockopt(peer.tcp_socket, SOL_SOCKET, SO_ERROR,
                                      reinterpret_cast<char*>(&connect_error), &error_length);
#else
        const int result = getsockopt(peer.tcp_socket, SOL_SOCKET, SO_ERROR,
                                      &connect_error, &error_length);
#endif
        if (result == detail::kSocketError) {
            return network_error("TCP connect status query failed", detail::last_socket_error());
        }
        if (connect_error != 0) return network_error("TCP connect failed", connect_error);

        peer.connecting = false;
        return start_client_hello(peer);
    }

    [[nodiscard]] snt::core::Expected<void> handle_tcp_packet(
        Peer& peer, const detail::WirePacket& packet, std::vector<ReplicationEvent>& events) {
        if (server) {
            switch (packet.kind) {
                case detail::WirePacketKind::TcpHello: {
                    if (peer.tcp_ready || packet.payload.size() != kAssociationTokenBytes) {
                        return protocol_error("Unexpected TCP hello packet");
                    }
                    std::copy(packet.payload.begin(), packet.payload.end(), peer.client_token.begin());
                    peer.server_token = generate_association_token();
                    peer.has_server_token = true;

                    detail::WirePacket acknowledgement;
                    acknowledgement.kind = detail::WirePacketKind::TcpHelloAck;
                    acknowledgement.channel = ReplicationChannel::Reliable;
                    acknowledgement.payload.insert(acknowledgement.payload.end(),
                                                   peer.client_token.begin(), peer.client_token.end());
                    acknowledgement.payload.insert(acknowledgement.payload.end(),
                                                   peer.server_token.begin(), peer.server_token.end());
                    if (auto result = queue_tcp_packet(peer, acknowledgement); !result) {
                        return result.error();
                    }
                    peer.tcp_ready = true;
                    events.push_back({.kind = ReplicationEventKind::PeerConnected, .peer = peer.id});
                    SNT_LOG_INFO("TCP replication peer %llu completed handshake",
                                 static_cast<unsigned long long>(peer.id));
                    return {};
                }
                case detail::WirePacketKind::Application:
                    if (!peer.tcp_ready || packet.channel != ReplicationChannel::Reliable) {
                        return protocol_error("TCP application packet arrived before handshake or on UDP channel");
                    }
                    events.push_back({
                        .kind = ReplicationEventKind::FrameReceived,
                        .peer = peer.id,
                        .frame = {
                            .protocol_version = config.protocol_version,
                            .server_tick = packet.server_tick,
                            .channel = packet.channel,
                            .payload = packet.payload,
                        },
                    });
                    return {};
                default:
                    return protocol_error("Unexpected TCP replication control packet");
            }
        }

        switch (packet.kind) {
            case detail::WirePacketKind::TcpHelloAck: {
                if (!peer.awaiting_hello_ack || packet.payload.size() != 2 * kAssociationTokenBytes ||
                    !token_equals(std::span<const std::byte>(packet.payload.data(),
                                                             kAssociationTokenBytes),
                                  peer.client_token)) {
                    return protocol_error("TCP hello acknowledgement does not match the client token");
                }
                std::copy(packet.payload.begin() + static_cast<std::ptrdiff_t>(kAssociationTokenBytes),
                          packet.payload.end(), peer.server_token.begin());
                peer.has_server_token = true;
                peer.awaiting_hello_ack = false;
                peer.tcp_ready = true;
                if (auto result = maybe_send_udp_association(peer, true); !result) return result.error();
                events.push_back({.kind = ReplicationEventKind::PeerConnected, .peer = peer.id});
                SNT_LOG_INFO("TCP replication server handshake completed");
                return {};
            }
            case detail::WirePacketKind::Application:
                if (!peer.tcp_ready || packet.channel != ReplicationChannel::Reliable) {
                    return protocol_error("TCP application packet arrived before handshake or on UDP channel");
                }
                events.push_back({
                    .kind = ReplicationEventKind::FrameReceived,
                    .peer = peer.id,
                    .frame = {
                        .protocol_version = config.protocol_version,
                        .server_tick = packet.server_tick,
                        .channel = packet.channel,
                        .payload = packet.payload,
                    },
                });
                return {};
            default:
                return protocol_error("Unexpected TCP replication control packet from server");
        }
    }

    [[nodiscard]] snt::core::Expected<void> decode_tcp_buffer(
        Peer& peer, std::vector<ReplicationEvent>& events) {
        while (!peer.receive_buffer.empty()) {
            const std::span<const std::byte> bytes(peer.receive_buffer.data(), peer.receive_buffer.size());
            auto packet_size = detail::try_wire_packet_size(bytes, config);
            if (!packet_size) return packet_size.error();
            if (!*packet_size) return {};

            const size_t complete_size = **packet_size;
            auto packet = detail::decode_wire_packet(bytes.first(complete_size), config);
            if (!packet) return packet.error();
            peer.receive_buffer.erase(peer.receive_buffer.begin(),
                                      peer.receive_buffer.begin() +
                                          static_cast<std::ptrdiff_t>(complete_size));
            if (auto result = handle_tcp_packet(peer, *packet, events); !result) return result.error();
        }
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> receive_tcp(
        Peer& peer, std::vector<ReplicationEvent>& events) {
        std::array<std::byte, kTcpReceiveChunkBytes> chunk{};
        while (true) {
            const int received = ::recv(peer.tcp_socket, reinterpret_cast<char*>(chunk.data()),
                                        static_cast<int>(chunk.size()), 0);
            if (received > 0) {
                peer.receive_buffer.insert(peer.receive_buffer.end(), chunk.begin(),
                                           chunk.begin() + received);
                if (auto result = decode_tcp_buffer(peer, events); !result) return result.error();
                continue;
            }
            if (received == 0) return network_error("TCP peer closed the connection", 0);
            const int error = detail::last_socket_error();
            if (detail::socket_would_block(error)) return {};
            return network_error("TCP receive failed", error);
        }
    }

    [[nodiscard]] snt::core::Expected<void> accept_tcp_connections() {
        for (size_t count = 0; count < kMaxAcceptsPerPoll; ++count) {
            sockaddr_in remote{};
            detail::SocketLength length = sizeof(remote);
            const detail::Socket accepted = ::accept(
                tcp_listener, reinterpret_cast<sockaddr*>(&remote), &length);
            if (accepted == detail::kInvalidSocket) {
                const int error = detail::last_socket_error();
                if (detail::socket_would_block(error)) return {};
                return network_error("TCP accept failed", error);
            }
            if (peers.size() >= config.max_peers) {
                detail::close_socket(accepted);
                warn_peer_limit_reached();
                continue;
            }
            if (auto result = configure_tcp_socket(accepted); !result) {
                detail::close_socket(accepted);
                return result.error();
            }

            Peer peer;
            peer.id = next_peer_id++;
            peer.tcp_socket = accepted;
            peers.emplace(peer.id, std::move(peer));
        }
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> handle_server_udp_packet(
        const Ipv4Endpoint& source, const detail::WirePacket& packet,
        std::vector<ReplicationEvent>& events) {
        switch (packet.kind) {
            case detail::WirePacketKind::UdpAssociate: {
                if (packet.payload.size() != kAssociationTokenBytes) {
                    return protocol_error("UDP association token has an invalid length");
                }
                Peer* matched_peer = nullptr;
                for (auto& [_, peer] : peers) {
                    if (peer.tcp_ready && peer.has_server_token &&
                        token_equals(packet.payload, peer.server_token)) {
                        matched_peer = &peer;
                        break;
                    }
                }
                if (matched_peer == nullptr) return protocol_error("UDP association token is unknown");
                matched_peer->udp_endpoint = source;
                matched_peer->udp_ready = true;

                detail::WirePacket acknowledgement;
                acknowledgement.kind = detail::WirePacketKind::UdpAssociateAck;
                acknowledgement.channel = ReplicationChannel::Unreliable;
                acknowledgement.payload.assign(matched_peer->server_token.begin(),
                                               matched_peer->server_token.end());
                return send_udp_packet(source, acknowledgement);
            }
            case detail::WirePacketKind::Application: {
                Peer* peer = find_peer_by_udp_endpoint(source);
                if (peer == nullptr || !peer->tcp_ready || !peer->udp_ready ||
                    packet.channel != ReplicationChannel::Unreliable) {
                    return protocol_error("UDP application packet has no associated peer");
                }
                events.push_back({
                    .kind = ReplicationEventKind::FrameReceived,
                    .peer = peer->id,
                    .frame = {
                        .protocol_version = config.protocol_version,
                        .server_tick = packet.server_tick,
                        .channel = packet.channel,
                        .payload = packet.payload,
                    },
                });
                return {};
            }
            default:
                return protocol_error("Unexpected UDP replication control packet at server");
        }
    }

    [[nodiscard]] snt::core::Expected<void> handle_client_udp_packet(
        const Ipv4Endpoint& source, const detail::WirePacket& packet,
        std::vector<ReplicationEvent>& events) {
        if (!remote_udp_endpoint || !same_endpoint(source, *remote_udp_endpoint)) {
            return protocol_error("UDP replication packet did not originate from the configured server");
        }
        Peer* peer = find_peer(kServerPeerId);
        if (peer == nullptr || !peer->tcp_ready) {
            return protocol_error("UDP replication packet arrived before the TCP handshake");
        }

        switch (packet.kind) {
            case detail::WirePacketKind::UdpAssociateAck:
                if (!peer->has_server_token || !token_equals(packet.payload, peer->server_token)) {
                    return protocol_error("UDP association acknowledgement token does not match");
                }
                peer->udp_ready = true;
                return {};
            case detail::WirePacketKind::Application:
                if (!peer->udp_ready || packet.channel != ReplicationChannel::Unreliable) {
                    return protocol_error("UDP application packet arrived before association");
                }
                events.push_back({
                    .kind = ReplicationEventKind::FrameReceived,
                    .peer = peer->id,
                    .frame = {
                        .protocol_version = config.protocol_version,
                        .server_tick = packet.server_tick,
                        .channel = packet.channel,
                        .payload = packet.payload,
                    },
                });
                return {};
            default:
                return protocol_error("Unexpected UDP replication control packet from server");
        }
    }

    [[nodiscard]] snt::core::Expected<void> receive_udp(
        std::vector<ReplicationEvent>& events) {
        std::array<std::byte, kUdpReceiveBufferBytes> buffer{};
        for (size_t count = 0; count < kMaxUdpDatagramsPerPoll; ++count) {
            Ipv4Endpoint source;
            detail::SocketLength length = sizeof(source.address);
            const int received = ::recvfrom(
                udp_socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                reinterpret_cast<sockaddr*>(&source.address), &length);
            if (received == detail::kSocketError) {
                const int error = detail::last_socket_error();
                if (detail::socket_would_block(error)) return {};
                return network_error("UDP receive failed", error);
            }
            if (received == 0) continue;

            const std::span<const std::byte> bytes(buffer.data(), static_cast<size_t>(received));
            auto packet = detail::decode_wire_packet(bytes, config);
            if (!packet) {
                if (Peer* peer = find_peer_by_udp_endpoint(source); peer != nullptr) {
                    disconnect_peer(peer->id, "UDP protocol error: " + packet.error().format(), &events);
                } else if (!server && remote_udp_endpoint && same_endpoint(source, *remote_udp_endpoint)) {
                    disconnect_peer(kServerPeerId, "UDP protocol error: " + packet.error().format(), &events);
                } else {
                    warn_invalid_udp(packet.error().format());
                }
                continue;
            }

            const auto result = server
                ? handle_server_udp_packet(source, *packet, events)
                : handle_client_udp_packet(source, *packet, events);
            if (result) continue;

            if (Peer* peer = find_peer_by_udp_endpoint(source); peer != nullptr) {
                disconnect_peer(peer->id, "UDP protocol error: " + result.error().format(), &events);
            } else if (!server && remote_udp_endpoint && same_endpoint(source, *remote_udp_endpoint)) {
                disconnect_peer(kServerPeerId, "UDP protocol error: " + result.error().format(), &events);
            } else {
                warn_invalid_udp(result.error().format());
            }
        }
        return {};
    }
};

TcpUdpReplicationTransport::TcpUdpReplicationTransport() : impl_(std::make_unique<Impl>()) {}

TcpUdpReplicationTransport::~TcpUdpReplicationTransport() { shutdown(); }

snt::core::Expected<std::unique_ptr<TcpUdpReplicationTransport>>
TcpUdpReplicationTransport::listen(TcpUdpListenConfig listen_config) {
    auto transport = std::unique_ptr<TcpUdpReplicationTransport>(new TcpUdpReplicationTransport());
    auto& impl = *transport->impl_;
    if (auto result = validate_replication_config(listen_config.replication); !result) {
        return result.error();
    }
    if (auto result = detail::initialize_socket_platform(); !result) return result.error();
    impl.socket_platform_initialized = true;
    impl.server = true;
    impl.config = listen_config.replication;

    auto endpoint = resolve_ipv4(listen_config.bind_address, listen_config.tcp_port, true);
    if (!endpoint) return endpoint.error();
    impl.tcp_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl.tcp_listener == detail::kInvalidSocket) {
        return network_error("Could not create TCP listener", detail::last_socket_error());
    }
    if (!detail::set_socket_reuse_address(impl.tcp_listener)) {
        return network_error("Could not enable TCP listener address reuse", detail::last_socket_error());
    }
    if (!detail::set_socket_non_blocking(impl.tcp_listener)) {
        return network_error("Could not set TCP listener non-blocking", detail::last_socket_error());
    }
    if (::bind(impl.tcp_listener, reinterpret_cast<const sockaddr*>(&endpoint->address),
               sizeof(endpoint->address)) == detail::kSocketError) {
        return network_error("Could not bind TCP listener", detail::last_socket_error());
    }
    if (::listen(impl.tcp_listener, static_cast<int>(listen_config.replication.max_peers)) ==
        detail::kSocketError) {
        return network_error("Could not listen on TCP socket", detail::last_socket_error());
    }
    auto tcp_port = bound_port(impl.tcp_listener);
    if (!tcp_port) return tcp_port.error();
    impl.bound_tcp_port = *tcp_port;

    auto udp_endpoint = resolve_ipv4(listen_config.bind_address, listen_config.udp_port, true);
    if (!udp_endpoint) return udp_endpoint.error();
    impl.udp_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl.udp_socket == detail::kInvalidSocket) {
        return network_error("Could not create UDP socket", detail::last_socket_error());
    }
    if (!detail::set_socket_reuse_address(impl.udp_socket)) {
        return network_error("Could not enable UDP socket address reuse", detail::last_socket_error());
    }
    if (auto result = configure_udp_socket(impl.udp_socket); !result) return result.error();
    if (::bind(impl.udp_socket, reinterpret_cast<const sockaddr*>(&udp_endpoint->address),
               sizeof(udp_endpoint->address)) == detail::kSocketError) {
        return network_error("Could not bind UDP socket", detail::last_socket_error());
    }
    auto udp_port = bound_port(impl.udp_socket);
    if (!udp_port) return udp_port.error();
    impl.bound_udp_port = *udp_port;

    SNT_LOG_INFO("TCP+UDP replication listener started on %s (tcp=%u udp=%u)",
                 listen_config.bind_address.c_str(), impl.bound_tcp_port, impl.bound_udp_port);
    return transport;
}

snt::core::Expected<std::unique_ptr<TcpUdpReplicationTransport>>
TcpUdpReplicationTransport::connect(TcpUdpConnectConfig connect_config) {
    auto transport = std::unique_ptr<TcpUdpReplicationTransport>(new TcpUdpReplicationTransport());
    auto& impl = *transport->impl_;
    if (auto result = validate_replication_config(connect_config.replication); !result) {
        return result.error();
    }
    if (auto result = detail::initialize_socket_platform(); !result) return result.error();
    impl.socket_platform_initialized = true;
    impl.server = false;
    impl.config = connect_config.replication;

    auto tcp_endpoint = resolve_ipv4(connect_config.host, connect_config.tcp_port, false);
    if (!tcp_endpoint) return tcp_endpoint.error();
    auto udp_endpoint = resolve_ipv4(connect_config.host, connect_config.udp_port, false);
    if (!udp_endpoint) return udp_endpoint.error();
    impl.remote_udp_endpoint = *udp_endpoint;

    impl.udp_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl.udp_socket == detail::kInvalidSocket) {
        return network_error("Could not create client UDP socket", detail::last_socket_error());
    }
    if (auto result = configure_udp_socket(impl.udp_socket); !result) return result.error();
    sockaddr_in local_udp{};
    local_udp.sin_family = AF_INET;
    local_udp.sin_addr.s_addr = htonl(INADDR_ANY);
    local_udp.sin_port = htons(0);
    if (::bind(impl.udp_socket, reinterpret_cast<const sockaddr*>(&local_udp), sizeof(local_udp)) ==
        detail::kSocketError) {
        return network_error("Could not bind client UDP socket", detail::last_socket_error());
    }
    auto udp_port = bound_port(impl.udp_socket);
    if (!udp_port) return udp_port.error();
    impl.bound_udp_port = *udp_port;

    impl.tcp_listener = detail::kInvalidSocket;
    const detail::Socket tcp_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_socket == detail::kInvalidSocket) {
        return network_error("Could not create client TCP socket", detail::last_socket_error());
    }
    if (auto result = configure_tcp_socket(tcp_socket); !result) {
        detail::close_socket(tcp_socket);
        return result.error();
    }

    PeerId peer_id = kServerPeerId;
    Impl::Peer peer;
    peer.id = peer_id;
    peer.tcp_socket = tcp_socket;
    const int connect_result = ::connect(tcp_socket,
                                         reinterpret_cast<const sockaddr*>(&tcp_endpoint->address),
                                         sizeof(tcp_endpoint->address));
    if (connect_result == detail::kSocketError) {
        const int error = detail::last_socket_error();
        if (!detail::socket_connect_in_progress(error)) {
            detail::close_socket(tcp_socket);
            return network_error("TCP connection attempt failed", error);
        }
        peer.connecting = true;
    }
    auto [iterator, inserted] = impl.peers.emplace(peer_id, std::move(peer));
    if (!inserted) return invalid_state("Client TCP transport already has a server peer");
    if (!iterator->second.connecting) {
        if (auto result = impl.start_client_hello(iterator->second); !result) return result.error();
    }

    SNT_LOG_INFO("TCP+UDP replication connection started to %s (tcp=%u udp=%u)",
                 connect_config.host.c_str(), connect_config.tcp_port, connect_config.udp_port);
    return transport;
}

snt::core::Expected<void> TcpUdpReplicationTransport::send(
    PeerId peer_id, const ReplicationFrame& frame) {
    if (!impl_ || impl_->stopped) return invalid_state("TCP+UDP transport is shut down");
    if (frame.protocol_version != impl_->config.protocol_version) {
        return protocol_error("Replication frame protocol version does not match transport configuration");
    }
    Impl::Peer* peer = impl_->find_peer(peer_id);
    if (peer == nullptr || !peer->tcp_ready) {
        return invalid_state("Replication frame targets a peer without a completed TCP handshake");
    }

    detail::WirePacket packet;
    packet.kind = detail::WirePacketKind::Application;
    packet.channel = frame.channel;
    packet.server_tick = frame.server_tick;
    packet.payload = frame.payload;
    if (frame.channel == ReplicationChannel::Reliable) return impl_->queue_tcp_packet(*peer, packet);
    if (frame.channel != ReplicationChannel::Unreliable) return protocol_error("Unknown replication channel");
    const Ipv4Endpoint* udp_endpoint = impl_->server
        ? (peer->udp_endpoint ? &*peer->udp_endpoint : nullptr)
        : (impl_->remote_udp_endpoint ? &*impl_->remote_udp_endpoint : nullptr);
    if (!peer->udp_ready || udp_endpoint == nullptr) {
        return invalid_state("Unreliable replication frame targets a peer without UDP association");
    }
    return impl_->send_udp_packet(*udp_endpoint, packet);
}

snt::core::Expected<void> TcpUdpReplicationTransport::disconnect(PeerId peer,
                                                                   std::string_view reason) {
    if (!impl_ || impl_->stopped) return invalid_state("TCP+UDP transport is shut down");
    if (impl_->find_peer(peer) == nullptr) {
        return invalid_argument("Cannot disconnect an unknown TCP+UDP replication peer");
    }
    SNT_LOG_INFO("TCP+UDP replication peer %llu disconnected locally: %.*s",
                 static_cast<unsigned long long>(peer), static_cast<int>(reason.size()), reason.data());
    impl_->disconnect_peer(peer, std::string(reason), nullptr);
    return {};
}

snt::core::Expected<std::vector<ReplicationEvent>> TcpUdpReplicationTransport::poll() {
    if (!impl_ || impl_->stopped) return invalid_state("TCP+UDP transport is shut down");
    std::vector<ReplicationEvent> events;

    if (impl_->server) {
        if (auto result = impl_->accept_tcp_connections(); !result) return result.error();
    }

    std::vector<PeerId> peer_ids;
    peer_ids.reserve(impl_->peers.size());
    for (const auto& [peer_id, _] : impl_->peers) peer_ids.push_back(peer_id);
    for (const PeerId peer_id : peer_ids) {
        Impl::Peer* peer = impl_->find_peer(peer_id);
        if (peer == nullptr) continue;
        snt::core::Expected<void> result;
        if (peer->connecting) result = impl_->finish_connect(*peer);
        if (result && !peer->connecting) result = impl_->receive_tcp(*peer, events);
        if (!result) {
            impl_->disconnect_peer(peer_id, "TCP replication connection ended: " + result.error().format(),
                                   &events);
        }
    }

    peer_ids.clear();
    peer_ids.reserve(impl_->peers.size());
    for (const auto& [peer_id, _] : impl_->peers) peer_ids.push_back(peer_id);
    for (const PeerId peer_id : peer_ids) {
        Impl::Peer* peer = impl_->find_peer(peer_id);
        if (peer == nullptr) continue;
        if (auto result = impl_->maybe_send_udp_association(*peer); !result) {
            impl_->disconnect_peer(peer_id, "UDP association failed: " + result.error().format(), &events);
            continue;
        }
        if (auto result = impl_->flush_tcp(*peer); !result) {
            impl_->disconnect_peer(peer_id, "TCP replication send failed: " + result.error().format(), &events);
        }
    }

    if (auto result = impl_->receive_udp(events); !result) return result.error();
    return events;
}

std::vector<PeerId> TcpUdpReplicationTransport::connected_peers() const {
    std::vector<PeerId> peers;
    if (!impl_ || impl_->stopped) return peers;
    peers.reserve(impl_->peers.size());
    for (const auto& [peer_id, peer] : impl_->peers) {
        if (peer.tcp_ready) peers.push_back(peer_id);
    }
    std::sort(peers.begin(), peers.end());
    return peers;
}

void TcpUdpReplicationTransport::shutdown() noexcept {
    if (!impl_ || impl_->stopped) return;
    impl_->stopped = true;
    impl_->close_all();
    SNT_LOG_INFO("TCP+UDP replication transport shut down");
}

bool TcpUdpReplicationTransport::is_server() const noexcept {
    return impl_ != nullptr && impl_->server;
}

bool TcpUdpReplicationTransport::is_unreliable_ready(PeerId peer_id) const noexcept {
    const Impl::Peer* peer = impl_ ? impl_->find_peer(peer_id) : nullptr;
    return peer != nullptr && peer->udp_ready;
}

uint16_t TcpUdpReplicationTransport::tcp_port() const noexcept {
    return impl_ ? impl_->bound_tcp_port : 0;
}

uint16_t TcpUdpReplicationTransport::udp_port() const noexcept {
    return impl_ ? impl_->bound_udp_port : 0;
}

}  // namespace snt::network
