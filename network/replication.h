// Replication contracts -- P7 server-authoritative networking boundary.
//
// This header intentionally contains no Steam or Godot dependency. A future
// SteamP2P transport and direct-LAN transport implement IReplicationTransport;
// ReplicationService owns protocol versioning and applies frames on the main
// thread. World mutation stays outside transport code.

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/expected.h"

namespace snt::ecs {
class World;
}

namespace snt::network {

using PeerId = uint64_t;

enum class ReplicationChannel : uint8_t {
    Reliable,
    Unreliable,
};

struct ReplicationFrame {
    uint16_t protocol_version = 1;
    uint64_t server_tick = 0;
    ReplicationChannel channel = ReplicationChannel::Reliable;
    std::vector<std::byte> payload;
};

class IReplicationTransport {
public:
    virtual ~IReplicationTransport() = default;

    virtual snt::core::Expected<void> send(PeerId peer,
                                           const ReplicationFrame& frame) = 0;
    virtual std::vector<std::pair<PeerId, ReplicationFrame>> receive() = 0;
};

class ReplicationService {
public:
    virtual ~ReplicationService() = default;

    virtual snt::core::Expected<void> host(snt::ecs::World& world,
                                           IReplicationTransport& transport) = 0;
    virtual snt::core::Expected<void> join(IReplicationTransport& transport) = 0;
    virtual void update(float delta_seconds) = 0;
    virtual void shutdown() = 0;
};

}  // namespace snt::network
