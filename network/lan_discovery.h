// Native IPv4 LAN server discovery.
//
// This module is intentionally independent from replication and game payloads.
// A responder advertises one application-defined server, while a client sends
// a broadcast query and polls replies on its caller-owned main thread.

#pragma once

#include "core/expected.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snt::network {

inline constexpr uint16_t kDefaultLanDiscoveryPort = 23587;
inline constexpr uint16_t kCurrentLanDiscoveryProtocolVersion = 1;
inline constexpr size_t kMaxLanDiscoveryServerNameBytes = 128;

// The application protocol version is deliberately opaque to this module.
// Clients can use it to reject servers whose game payload codec differs.
struct LanDiscoveryAdvertisement {
    uint16_t application_protocol_version = 1;
    std::string server_name;
    uint16_t tcp_port = 0;
    uint16_t udp_port = 0;
    uint16_t current_players = 0;
    uint16_t max_players = 0;
    bool password_required = false;
};

struct LanDiscoveryResponderConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t port = kDefaultLanDiscoveryPort;
};

struct LanDiscoveryClientConfig {
    std::string target_address = "255.255.255.255";
    uint16_t port = kDefaultLanDiscoveryPort;
};

struct LanDiscoveredServer {
    std::string host;
    LanDiscoveryAdvertisement advertisement;
};

class LanDiscoveryResponder final {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<LanDiscoveryResponder>> listen(
        LanDiscoveryResponderConfig config = {});

    ~LanDiscoveryResponder();

    LanDiscoveryResponder(const LanDiscoveryResponder&) = delete;
    LanDiscoveryResponder& operator=(const LanDiscoveryResponder&) = delete;

    // Replies to all currently queued requests using the supplied current
    // advertisement. It never invokes game code or starts socket threads.
    [[nodiscard]] snt::core::Expected<size_t> poll(
        const LanDiscoveryAdvertisement& advertisement);
    [[nodiscard]] uint16_t port() const noexcept;
    void shutdown() noexcept;

private:
    struct Impl;

    LanDiscoveryResponder();
    std::unique_ptr<Impl> impl_;
};

class LanDiscoveryClient final {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<LanDiscoveryClient>> create(
        LanDiscoveryClientConfig config = {});

    ~LanDiscoveryClient();

    LanDiscoveryClient(const LanDiscoveryClient&) = delete;
    LanDiscoveryClient& operator=(const LanDiscoveryClient&) = delete;

    // Sends one discovery query. Call poll() afterwards until the desired
    // browse window expires; poll() returns only replies received since the
    // previous call.
    [[nodiscard]] snt::core::Expected<void> query();
    [[nodiscard]] snt::core::Expected<std::vector<LanDiscoveredServer>> poll();
    [[nodiscard]] uint16_t local_port() const noexcept;
    void shutdown() noexcept;

private:
    struct Impl;

    LanDiscoveryClient();
    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::network
