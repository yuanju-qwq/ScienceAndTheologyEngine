// Native IPv4 LAN discovery implementation.

#define SNT_LOG_CHANNEL "network.lan_discovery"
#include "network/lan_discovery.h"

#include "network/socket_platform.h"

#include "core/error.h"
#include "core/log.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace snt::network {
namespace {

constexpr uint32_t kLanDiscoveryMagic = 0x534E5444u;  // "SNTD"
constexpr uint8_t kLanDiscoveryRequest = 1;
constexpr uint8_t kLanDiscoveryReply = 2;
constexpr size_t kLanDiscoveryHeaderBytes = 10;
constexpr size_t kMaxLanDiscoveryDatagramBytes = 512;
constexpr size_t kMaxDatagramsPerPoll = 64;
constexpr uint8_t kLanDiscoveryFlagPasswordRequired = 1u << 0u;

struct Ipv4Endpoint {
    sockaddr_in address{};
};

struct DecodedPacket {
    uint8_t kind = 0;
    std::span<const std::byte> payload;
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

void append_u16(std::vector<std::byte>& bytes, uint16_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::byte>(value & 0xffu));
}

void append_u32(std::vector<std::byte>& bytes, uint32_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 24u) & 0xffu));
    bytes.push_back(static_cast<std::byte>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::byte>(value & 0xffu));
}

[[nodiscard]] uint16_t read_u16(std::span<const std::byte> bytes, size_t offset) {
    return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset])) << 8u |
           static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset + 1]));
}

[[nodiscard]] uint32_t read_u32(std::span<const std::byte> bytes, size_t offset) {
    return static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset])) << 24u |
           static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 1])) << 16u |
           static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 2])) << 8u |
           static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[offset + 3]));
}

[[nodiscard]] snt::core::Expected<void> validate_advertisement(
    const LanDiscoveryAdvertisement& advertisement) {
    if (advertisement.application_protocol_version == 0) {
        return invalid_argument("LAN discovery application protocol version must be non-zero");
    }
    if (advertisement.server_name.empty() ||
        advertisement.server_name.size() > kMaxLanDiscoveryServerNameBytes) {
        return invalid_argument("LAN discovery server name must contain 1-128 bytes");
    }
    if (advertisement.tcp_port == 0 || advertisement.udp_port == 0) {
        return invalid_argument("LAN discovery advertisement requires TCP and UDP ports");
    }
    if (advertisement.max_players == 0 ||
        advertisement.current_players > advertisement.max_players) {
        return invalid_argument("LAN discovery player counts are invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::vector<std::byte>> encode_packet(
    uint8_t kind, std::span<const std::byte> payload) {
    if (payload.size() > std::numeric_limits<uint16_t>::max() ||
        payload.size() + kLanDiscoveryHeaderBytes > kMaxLanDiscoveryDatagramBytes) {
        return invalid_argument("LAN discovery packet exceeds its datagram limit");
    }

    std::vector<std::byte> bytes;
    bytes.reserve(kLanDiscoveryHeaderBytes + payload.size());
    append_u32(bytes, kLanDiscoveryMagic);
    append_u16(bytes, kCurrentLanDiscoveryProtocolVersion);
    bytes.push_back(static_cast<std::byte>(kind));
    bytes.push_back(std::byte{0});
    append_u16(bytes, static_cast<uint16_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

[[nodiscard]] snt::core::Expected<DecodedPacket> decode_packet(
    std::span<const std::byte> bytes) {
    if (bytes.size() < kLanDiscoveryHeaderBytes) {
        return protocol_error("LAN discovery packet is incomplete");
    }
    if (read_u32(bytes, 0) != kLanDiscoveryMagic) {
        return protocol_error("LAN discovery magic does not match");
    }
    if (read_u16(bytes, 4) != kCurrentLanDiscoveryProtocolVersion) {
        return protocol_error("LAN discovery protocol version does not match");
    }
    if (std::to_integer<uint8_t>(bytes[7]) != 0) {
        return protocol_error("LAN discovery reserved header bits are non-zero");
    }
    const size_t payload_size = read_u16(bytes, 8);
    if (kLanDiscoveryHeaderBytes + payload_size != bytes.size()) {
        return protocol_error("LAN discovery packet has an invalid payload length");
    }
    return DecodedPacket{
        .kind = std::to_integer<uint8_t>(bytes[6]),
        .payload = bytes.subspan(kLanDiscoveryHeaderBytes),
    };
}

[[nodiscard]] snt::core::Expected<std::vector<std::byte>> encode_advertisement(
    const LanDiscoveryAdvertisement& advertisement) {
    if (auto result = validate_advertisement(advertisement); !result) return result.error();

    std::vector<std::byte> payload;
    payload.reserve(12 + advertisement.server_name.size());
    append_u16(payload, advertisement.application_protocol_version);
    append_u16(payload, advertisement.tcp_port);
    append_u16(payload, advertisement.udp_port);
    append_u16(payload, advertisement.current_players);
    append_u16(payload, advertisement.max_players);
    payload.push_back(static_cast<std::byte>(
        advertisement.password_required ? kLanDiscoveryFlagPasswordRequired : 0));
    payload.push_back(static_cast<std::byte>(advertisement.server_name.size()));
    for (const unsigned char character : advertisement.server_name) {
        payload.push_back(static_cast<std::byte>(character));
    }
    return payload;
}

[[nodiscard]] snt::core::Expected<LanDiscoveryAdvertisement> decode_advertisement(
    std::span<const std::byte> payload) {
    constexpr size_t kFixedAdvertisementBytes = 12;
    if (payload.size() < kFixedAdvertisementBytes) {
        return protocol_error("LAN discovery advertisement is incomplete");
    }

    LanDiscoveryAdvertisement advertisement;
    advertisement.application_protocol_version = read_u16(payload, 0);
    advertisement.tcp_port = read_u16(payload, 2);
    advertisement.udp_port = read_u16(payload, 4);
    advertisement.current_players = read_u16(payload, 6);
    advertisement.max_players = read_u16(payload, 8);
    const uint8_t flags = std::to_integer<uint8_t>(payload[10]);
    if ((flags & ~kLanDiscoveryFlagPasswordRequired) != 0) {
        return protocol_error("LAN discovery advertisement flags are invalid");
    }
    advertisement.password_required = (flags & kLanDiscoveryFlagPasswordRequired) != 0;
    const size_t name_size = std::to_integer<uint8_t>(payload[11]);
    if (name_size > kMaxLanDiscoveryServerNameBytes ||
        kFixedAdvertisementBytes + name_size != payload.size()) {
        return protocol_error("LAN discovery advertisement server name is invalid");
    }
    advertisement.server_name.reserve(name_size);
    for (size_t index = 0; index < name_size; ++index) {
        advertisement.server_name.push_back(static_cast<char>(
            std::to_integer<unsigned char>(payload[kFixedAdvertisementBytes + index])));
    }
    if (auto result = validate_advertisement(advertisement); !result) {
        return protocol_error(result.error().message());
    }
    return advertisement;
}

[[nodiscard]] snt::core::Expected<Ipv4Endpoint> resolve_ipv4(
    const std::string& host, uint16_t port, bool passive) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = passive ? AI_PASSIVE : 0;

    const std::string service = std::to_string(port);
    addrinfo* results = nullptr;
    const char* host_value = host.empty() ? nullptr : host.c_str();
    const int result = getaddrinfo(host_value, service.c_str(), &hints, &results);
    if (result != 0 || results == nullptr) {
        if (results != nullptr) freeaddrinfo(results);
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

[[nodiscard]] snt::core::Expected<std::string> endpoint_host(const sockaddr_in& address) {
    std::array<char, INET_ADDRSTRLEN> text{};
#if defined(_WIN32)
    const auto text_length = static_cast<DWORD>(text.size());
#else
    const auto text_length = static_cast<socklen_t>(text.size());
#endif
    if (inet_ntop(AF_INET, &address.sin_addr, text.data(), text_length) == nullptr) {
        return network_error("inet_ntop failed", detail::last_socket_error());
    }
    return std::string(text.data());
}

[[nodiscard]] snt::core::Expected<void> configure_udp_socket(detail::Socket socket,
                                                              bool enable_broadcast) {
    if (!detail::set_socket_non_blocking(socket)) {
        return network_error("Could not set LAN discovery UDP socket non-blocking",
                             detail::last_socket_error());
    }
    if (enable_broadcast && !detail::set_socket_broadcast(socket)) {
        return network_error("Could not enable LAN discovery UDP broadcast",
                             detail::last_socket_error());
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> send_datagram(detail::Socket socket,
                                                       const sockaddr_in& target,
                                                       std::span<const std::byte> bytes) {
    const int sent = ::sendto(socket, reinterpret_cast<const char*>(bytes.data()),
                              static_cast<int>(bytes.size()), 0,
                              reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    if (sent == detail::kSocketError) {
        const int error = detail::last_socket_error();
        if (detail::socket_would_block(error)) {
            return snt::core::Error{snt::core::ErrorCode::kNetworkIoFailed,
                                    "LAN discovery UDP send would block"};
        }
        return network_error("LAN discovery UDP send failed", error);
    }
    if (static_cast<size_t>(sent) != bytes.size()) {
        return snt::core::Error{snt::core::ErrorCode::kNetworkIoFailed,
                                "LAN discovery UDP send was truncated"};
    }
    return {};
}

}  // namespace

struct LanDiscoveryResponder::Impl {
    bool socket_platform_initialized = false;
    bool stopped = false;
    detail::Socket socket = detail::kInvalidSocket;
    uint16_t bound_port = 0;

    ~Impl() { close(); }

    void close() noexcept {
        detail::close_socket(socket);
        socket = detail::kInvalidSocket;
        if (socket_platform_initialized) {
            detail::shutdown_socket_platform();
            socket_platform_initialized = false;
        }
    }
};

LanDiscoveryResponder::LanDiscoveryResponder() : impl_(std::make_unique<Impl>()) {}

LanDiscoveryResponder::~LanDiscoveryResponder() { shutdown(); }

snt::core::Expected<std::unique_ptr<LanDiscoveryResponder>> LanDiscoveryResponder::listen(
    LanDiscoveryResponderConfig config) {
    auto responder = std::unique_ptr<LanDiscoveryResponder>(new LanDiscoveryResponder());
    auto& impl = *responder->impl_;
    if (auto result = detail::initialize_socket_platform(); !result) return result.error();
    impl.socket_platform_initialized = true;

    auto endpoint = resolve_ipv4(config.bind_address, config.port, true);
    if (!endpoint) return endpoint.error();
    impl.socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl.socket == detail::kInvalidSocket) {
        return network_error("Could not create LAN discovery responder socket",
                             detail::last_socket_error());
    }
    if (!detail::set_socket_reuse_address(impl.socket)) {
        return network_error("Could not enable LAN discovery responder address reuse",
                             detail::last_socket_error());
    }
    if (auto result = configure_udp_socket(impl.socket, false); !result) return result.error();
    if (::bind(impl.socket, reinterpret_cast<const sockaddr*>(&endpoint->address),
               sizeof(endpoint->address)) == detail::kSocketError) {
        return network_error("Could not bind LAN discovery responder", detail::last_socket_error());
    }
    auto port = bound_port(impl.socket);
    if (!port) return port.error();
    impl.bound_port = *port;
    SNT_LOG_INFO("LAN discovery responder listening on %s:%u",
                 config.bind_address.c_str(), impl.bound_port);
    return responder;
}

snt::core::Expected<size_t> LanDiscoveryResponder::poll(
    const LanDiscoveryAdvertisement& advertisement) {
    if (impl_->stopped) return invalid_state("LAN discovery responder is stopped");
    auto advertisement_payload = encode_advertisement(advertisement);
    if (!advertisement_payload) return advertisement_payload.error();
    auto reply = encode_packet(kLanDiscoveryReply, *advertisement_payload);
    if (!reply) return reply.error();

    size_t replies = 0;
    std::array<std::byte, kMaxLanDiscoveryDatagramBytes> buffer{};
    for (size_t index = 0; index < kMaxDatagramsPerPoll; ++index) {
        sockaddr_in source{};
        detail::SocketLength source_length = sizeof(source);
        const int received = ::recvfrom(
            impl_->socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&source), &source_length);
        if (received == detail::kSocketError) {
            const int error = detail::last_socket_error();
            if (detail::socket_would_block(error)) break;
            return network_error("LAN discovery responder receive failed", error);
        }
        if (received == 0) continue;

        auto packet = decode_packet(std::span<const std::byte>(
            buffer.data(), static_cast<size_t>(received)));
        if (!packet || packet->kind != kLanDiscoveryRequest || !packet->payload.empty()) continue;
        if (auto result = send_datagram(impl_->socket, source, *reply); !result) return result.error();
        ++replies;
    }
    return replies;
}

uint16_t LanDiscoveryResponder::port() const noexcept { return impl_->bound_port; }

void LanDiscoveryResponder::shutdown() noexcept {
    if (!impl_ || impl_->stopped) return;
    impl_->stopped = true;
    impl_->close();
}

struct LanDiscoveryClient::Impl {
    bool socket_platform_initialized = false;
    bool stopped = false;
    detail::Socket socket = detail::kInvalidSocket;
    Ipv4Endpoint target;
    uint16_t bound_port = 0;

    ~Impl() { close(); }

    void close() noexcept {
        detail::close_socket(socket);
        socket = detail::kInvalidSocket;
        if (socket_platform_initialized) {
            detail::shutdown_socket_platform();
            socket_platform_initialized = false;
        }
    }
};

LanDiscoveryClient::LanDiscoveryClient() : impl_(std::make_unique<Impl>()) {}

LanDiscoveryClient::~LanDiscoveryClient() { shutdown(); }

snt::core::Expected<std::unique_ptr<LanDiscoveryClient>> LanDiscoveryClient::create(
    LanDiscoveryClientConfig config) {
    auto client = std::unique_ptr<LanDiscoveryClient>(new LanDiscoveryClient());
    auto& impl = *client->impl_;
    if (auto result = detail::initialize_socket_platform(); !result) return result.error();
    impl.socket_platform_initialized = true;

    auto target = resolve_ipv4(config.target_address, config.port, false);
    if (!target) return target.error();
    impl.target = *target;
    impl.socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl.socket == detail::kInvalidSocket) {
        return network_error("Could not create LAN discovery client socket", detail::last_socket_error());
    }
    if (auto result = configure_udp_socket(impl.socket, true); !result) return result.error();
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(0);
    if (::bind(impl.socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) ==
        detail::kSocketError) {
        return network_error("Could not bind LAN discovery client", detail::last_socket_error());
    }
    auto port = bound_port(impl.socket);
    if (!port) return port.error();
    impl.bound_port = *port;
    return client;
}

snt::core::Expected<void> LanDiscoveryClient::query() {
    if (impl_->stopped) return invalid_state("LAN discovery client is stopped");
    auto request = encode_packet(kLanDiscoveryRequest, {});
    if (!request) return request.error();
    return send_datagram(impl_->socket, impl_->target.address, *request);
}

snt::core::Expected<std::vector<LanDiscoveredServer>> LanDiscoveryClient::poll() {
    if (impl_->stopped) return invalid_state("LAN discovery client is stopped");
    std::vector<LanDiscoveredServer> servers;
    std::array<std::byte, kMaxLanDiscoveryDatagramBytes> buffer{};
    for (size_t index = 0; index < kMaxDatagramsPerPoll; ++index) {
        sockaddr_in source{};
        detail::SocketLength source_length = sizeof(source);
        const int received = ::recvfrom(
            impl_->socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&source), &source_length);
        if (received == detail::kSocketError) {
            const int error = detail::last_socket_error();
            if (detail::socket_would_block(error)) break;
            return network_error("LAN discovery client receive failed", error);
        }
        if (received == 0) continue;

        auto packet = decode_packet(std::span<const std::byte>(
            buffer.data(), static_cast<size_t>(received)));
        if (!packet || packet->kind != kLanDiscoveryReply) continue;
        auto advertisement = decode_advertisement(packet->payload);
        if (!advertisement) continue;
        auto host = endpoint_host(source);
        if (!host) return host.error();
        servers.push_back({.host = std::move(*host), .advertisement = std::move(*advertisement)});
    }
    return servers;
}

uint16_t LanDiscoveryClient::local_port() const noexcept { return impl_->bound_port; }

void LanDiscoveryClient::shutdown() noexcept {
    if (!impl_ || impl_->stopped) return;
    impl_->stopped = true;
    impl_->close();
}

}  // namespace snt::network
