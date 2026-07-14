// Replication fixed-tick phase coordinator.

#define SNT_LOG_CHANNEL "network.replication"
#include "network/replication.h"

#include "core/error.h"
#include "core/log.h"

#include <string>
#include <unordered_set>

namespace snt::network {
namespace {

[[nodiscard]] snt::core::Error invalid_service_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

}  // namespace

ReplicationService::ReplicationService(IReplicationTransport& transport,
                                       IReplicationHandler& handler)
    : transport_(&transport), handler_(&handler) {}

snt::core::Expected<void> ReplicationService::poll_inbound(
    const ReplicationTickContext& context) {
    if (shutdown_ || transport_ == nullptr || handler_ == nullptr) {
        return invalid_service_state("ReplicationService::poll_inbound called after shutdown");
    }

    auto events = transport_->poll();
    if (!events) {
        auto error = events.error();
        error.with_context("ReplicationService::poll_inbound(transport poll)");
        return error;
    }

    std::unordered_set<PeerId> locally_rejected_peers;
    for (const auto& event : *events) {
        if (event.peer == kInvalidPeerId) {
            return snt::core::Error{snt::core::ErrorCode::kProtocolError,
                                    "Replication transport emitted an invalid peer id"};
        }
        if (locally_rejected_peers.contains(event.peer)) continue;

        switch (event.kind) {
            case ReplicationEventKind::PeerConnected: {
                if (auto result = handler_->on_peer_connected(event.peer, context); !result) {
                    const std::string reason = "game rejected peer: " + result.error().format();
                    SNT_LOG_WARN("Peer %llu rejected during connection: %s",
                                 static_cast<unsigned long long>(event.peer), reason.c_str());
                    handler_->on_peer_disconnected(event.peer, reason);
                    if (auto disconnect_result = transport_->disconnect(event.peer, reason);
                        !disconnect_result) {
                        auto error = disconnect_result.error();
                        error.with_context("ReplicationService::poll_inbound(reject peer)");
                        return error;
                    }
                    locally_rejected_peers.insert(event.peer);
                }
                break;
            }
            case ReplicationEventKind::FrameReceived: {
                if (auto result = handler_->on_frame(event.peer, event.frame, context); !result) {
                    const std::string reason = "game rejected replication frame: " +
                                               result.error().format();
                    SNT_LOG_WARN("Peer %llu sent a rejected replication frame: %s",
                                 static_cast<unsigned long long>(event.peer), reason.c_str());
                    handler_->on_peer_disconnected(event.peer, reason);
                    if (auto disconnect_result = transport_->disconnect(event.peer, reason);
                        !disconnect_result) {
                        auto error = disconnect_result.error();
                        error.with_context("ReplicationService::poll_inbound(reject frame)");
                        return error;
                    }
                    locally_rejected_peers.insert(event.peer);
                }
                break;
            }
            case ReplicationEventKind::PeerDisconnected:
                handler_->on_peer_disconnected(event.peer, event.detail);
                break;
        }
    }
    return {};
}

snt::core::Expected<void> ReplicationService::emit_outbound(
    const ReplicationTickContext& context) {
    if (shutdown_ || transport_ == nullptr || handler_ == nullptr) {
        return invalid_service_state("ReplicationService::emit_outbound called after shutdown");
    }
    if (auto result = handler_->emit_outbound(context, *this); !result) {
        auto error = result.error();
        error.with_context("ReplicationService::emit_outbound(handler)");
        return error;
    }
    return {};
}

void ReplicationService::shutdown() noexcept {
    if (shutdown_) return;
    shutdown_ = true;
    if (transport_) transport_->shutdown();
}

snt::core::Expected<void> ReplicationService::send(PeerId peer,
                                                    const ReplicationFrame& frame) {
    if (shutdown_ || transport_ == nullptr) {
        return invalid_service_state("ReplicationService::send called after shutdown");
    }
    return transport_->send(peer, frame);
}

snt::core::Expected<void> ReplicationService::broadcast(const ReplicationFrame& frame) {
    if (shutdown_ || transport_ == nullptr) {
        return invalid_service_state("ReplicationService::broadcast called after shutdown");
    }
    for (const PeerId peer : transport_->connected_peers()) {
        if (auto result = transport_->send(peer, frame); !result) return result.error();
    }
    return {};
}

}  // namespace snt::network
