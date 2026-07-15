// Network replication integration and adapter tests.

#include "network/replication.h"
#include "network/replication_wire.h"
#include "network/steam_p2p_transport.h"
#include "network/tcp_udp_transport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> bytes_from_text(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char value : text) bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

std::string text_from_bytes(const std::vector<std::byte>& bytes) {
    std::string text;
    text.reserve(bytes.size());
    for (const std::byte value : bytes) text.push_back(static_cast<char>(value));
    return text;
}

template <typename Predicate>
bool poll_until(snt::network::TcpUdpReplicationTransport& server,
                snt::network::TcpUdpReplicationTransport& client,
                Predicate predicate, std::vector<snt::network::ReplicationEvent>* server_events = nullptr,
                std::vector<snt::network::ReplicationEvent>* client_events = nullptr) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        auto client_poll = client.poll();
        if (!client_poll) return false;
        if (client_events) {
            client_events->insert(client_events->end(), client_poll->begin(), client_poll->end());
        }

        auto server_poll = server.poll();
        if (!server_poll) return false;
        if (server_events) {
            server_events->insert(server_events->end(), server_poll->begin(), server_poll->end());
        }

        auto client_poll_after_server = client.poll();
        if (!client_poll_after_server) return false;
        if (client_events) {
            client_events->insert(client_events->end(), client_poll_after_server->begin(),
                                  client_poll_after_server->end());
        }

        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

class FakeReplicationTransport final : public snt::network::IReplicationTransport {
public:
    snt::core::Expected<void> send(snt::network::PeerId peer,
                                   const snt::network::ReplicationFrame& frame) override {
        sent.emplace_back(peer, frame);
        return {};
    }

    snt::core::Expected<void> disconnect(snt::network::PeerId peer,
                                         std::string_view reason) override {
        disconnected.emplace_back(peer, std::string(reason));
        peers.erase(std::remove(peers.begin(), peers.end(), peer), peers.end());
        return {};
    }

    snt::core::Expected<std::vector<snt::network::ReplicationEvent>> poll() override {
        auto result = std::move(events);
        events.clear();
        return result;
    }

    std::vector<snt::network::PeerId> connected_peers() const override { return peers; }
    void shutdown() noexcept override { shutdown_called = true; }

    std::vector<snt::network::ReplicationEvent> events;
    std::vector<snt::network::PeerId> peers;
    std::vector<std::pair<snt::network::PeerId, snt::network::ReplicationFrame>> sent;
    std::vector<std::pair<snt::network::PeerId, std::string>> disconnected;
    bool shutdown_called = false;
};

class RecordingReplicationHandler final : public snt::network::IReplicationHandler {
public:
    snt::core::Expected<void> on_peer_connected(
        snt::network::PeerId peer, const snt::network::ReplicationTickContext&) override {
        calls.push_back("connected:" + std::to_string(peer));
        if (reject_connections) {
            return snt::core::Error{snt::core::ErrorCode::kProtocolError,
                                    "test handler rejects this peer"};
        }
        return {};
    }

    snt::core::Expected<void> on_frame(
        snt::network::PeerId peer, const snt::network::ReplicationFrame& frame,
        const snt::network::ReplicationTickContext&) override {
        calls.push_back("frame:" + std::to_string(peer) + ":" + text_from_bytes(frame.payload));
        return {};
    }

    void on_peer_disconnected(snt::network::PeerId peer, std::string_view) noexcept override {
        calls.push_back("disconnected:" + std::to_string(peer));
    }

    snt::core::Expected<void> emit_outbound(
        const snt::network::ReplicationTickContext&, snt::network::IReplicationFrameSink& sink) override {
        calls.push_back("emit");
        if (disconnect_during_emit) {
            return sink.disconnect(disconnect_peer, "test outbound disconnect");
        }
        snt::network::ReplicationFrame frame;
        frame.server_tick = 42;
        frame.payload = bytes_from_text("outbound");
        return sink.broadcast(frame);
    }

    std::vector<std::string> calls;
    bool reject_connections = false;
    bool disconnect_during_emit = false;
    snt::network::PeerId disconnect_peer = snt::network::kInvalidPeerId;
};

class FakeSteamBackend final : public snt::network::ISteamP2PBackend {
public:
    snt::core::Expected<void> send(snt::network::SteamPeerId peer,
                                   snt::network::ReplicationChannel channel,
                                   std::span<const std::byte> bytes) override {
        snt::network::SteamP2PBackendEvent event;
        event.peer = peer;
        event.channel = channel;
        event.bytes.assign(bytes.begin(), bytes.end());
        sent.push_back(std::move(event));
        return {};
    }

    snt::core::Expected<void> disconnect(snt::network::SteamPeerId peer,
                                         std::string_view reason) override {
        disconnects.emplace_back(peer, std::string(reason));
        return {};
    }

    snt::core::Expected<std::vector<snt::network::SteamP2PBackendEvent>> poll() override {
        auto result = std::move(events);
        events.clear();
        return result;
    }

    std::vector<snt::network::SteamP2PBackendEvent> events;
    std::vector<snt::network::SteamP2PBackendEvent> sent;
    std::vector<std::pair<snt::network::SteamPeerId, std::string>> disconnects;
};

TEST(ReplicationWireTest, RejectsProtocolVersionAndOversizedPayload) {
    snt::network::ReplicationTransportConfig config;
    snt::network::detail::WirePacket packet;
    packet.kind = snt::network::detail::WirePacketKind::Application;
    packet.channel = snt::network::ReplicationChannel::Reliable;
    packet.payload.resize(config.max_reliable_payload_bytes + 1);
    const auto oversized = snt::network::detail::encode_wire_packet(packet, config);
    ASSERT_FALSE(oversized);
    EXPECT_EQ(oversized.error().code(), snt::core::ErrorCode::kProtocolError);

    packet.payload = bytes_from_text("version");
    auto encoded = snt::network::detail::encode_wire_packet(packet, config);
    ASSERT_TRUE(encoded) << encoded.error().format();
    (*encoded)[5] = static_cast<std::byte>(2);
    const auto decoded = snt::network::detail::decode_wire_packet(*encoded, config);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code(), snt::core::ErrorCode::kProtocolError);
}

TEST(TcpUdpReplicationTransportTest, ExchangesReliableAndAssociatedUdpFrames) {
    auto server_result = snt::network::TcpUdpReplicationTransport::listen({
        .bind_address = "127.0.0.1",
        .tcp_port = 0,
        .udp_port = 0,
    });
    ASSERT_TRUE(server_result) << server_result.error().format();
    auto server = std::move(*server_result);

    auto client_result = snt::network::TcpUdpReplicationTransport::connect({
        .host = "127.0.0.1",
        .tcp_port = server->tcp_port(),
        .udp_port = server->udp_port(),
    });
    ASSERT_TRUE(client_result) << client_result.error().format();
    auto client = std::move(*client_result);

    std::vector<snt::network::ReplicationEvent> server_events;
    std::vector<snt::network::ReplicationEvent> client_events;
    ASSERT_TRUE(poll_until(*server, *client, [&] {
        const auto peers = server->connected_peers();
        return peers.size() == 1 && client->connected_peers().size() == 1 &&
               server->is_unreliable_ready(peers.front()) &&
               client->is_unreliable_ready(snt::network::kServerPeerId);
    }, &server_events, &client_events));

    const auto server_peers = server->connected_peers();
    ASSERT_EQ(server_peers.size(), 1u);
    EXPECT_TRUE(std::any_of(server_events.begin(), server_events.end(), [&](const auto& event) {
        return event.kind == snt::network::ReplicationEventKind::PeerConnected &&
               event.peer == server_peers.front();
    }));
    EXPECT_TRUE(std::any_of(client_events.begin(), client_events.end(), [](const auto& event) {
        return event.kind == snt::network::ReplicationEventKind::PeerConnected &&
               event.peer == snt::network::kServerPeerId;
    }));

    snt::network::ReplicationFrame reliable;
    reliable.server_tick = 11;
    reliable.channel = snt::network::ReplicationChannel::Reliable;
    reliable.payload = bytes_from_text("reliable");
    ASSERT_TRUE(client->send(snt::network::kServerPeerId, reliable));
    ASSERT_TRUE(poll_until(*server, *client, [&] {
        return std::any_of(server_events.begin(), server_events.end(), [](const auto& event) {
            return event.kind == snt::network::ReplicationEventKind::FrameReceived &&
                   event.frame.channel == snt::network::ReplicationChannel::Reliable &&
                   text_from_bytes(event.frame.payload) == "reliable";
        });
    }, &server_events, &client_events));

    snt::network::ReplicationFrame unreliable;
    unreliable.server_tick = 12;
    unreliable.channel = snt::network::ReplicationChannel::Unreliable;
    unreliable.payload = bytes_from_text("unreliable");
    ASSERT_TRUE(client->send(snt::network::kServerPeerId, unreliable));
    ASSERT_TRUE(poll_until(*server, *client, [&] {
        return std::any_of(server_events.begin(), server_events.end(), [](const auto& event) {
            return event.kind == snt::network::ReplicationEventKind::FrameReceived &&
                   event.frame.channel == snt::network::ReplicationChannel::Unreliable &&
                   text_from_bytes(event.frame.payload) == "unreliable";
        });
    }, &server_events, &client_events));

    snt::network::ReplicationFrame server_reliable;
    server_reliable.server_tick = 13;
    server_reliable.channel = snt::network::ReplicationChannel::Reliable;
    server_reliable.payload = bytes_from_text("server-reliable");
    ASSERT_TRUE(server->send(server_peers.front(), server_reliable));
    ASSERT_TRUE(poll_until(*server, *client, [&] {
        return std::any_of(client_events.begin(), client_events.end(), [](const auto& event) {
            return event.kind == snt::network::ReplicationEventKind::FrameReceived &&
                   event.frame.channel == snt::network::ReplicationChannel::Reliable &&
                   text_from_bytes(event.frame.payload) == "server-reliable";
        });
    }, &server_events, &client_events));

    snt::network::ReplicationFrame server_unreliable;
    server_unreliable.server_tick = 14;
    server_unreliable.channel = snt::network::ReplicationChannel::Unreliable;
    server_unreliable.payload = bytes_from_text("server-unreliable");
    ASSERT_TRUE(server->send(server_peers.front(), server_unreliable));
    ASSERT_TRUE(poll_until(*server, *client, [&] {
        return std::any_of(client_events.begin(), client_events.end(), [](const auto& event) {
            return event.kind == snt::network::ReplicationEventKind::FrameReceived &&
                   event.frame.channel == snt::network::ReplicationChannel::Unreliable &&
                   text_from_bytes(event.frame.payload) == "server-unreliable";
        });
    }, &server_events, &client_events));

    reliable.protocol_version = 99;
    const auto wrong_version = client->send(snt::network::kServerPeerId, reliable);
    EXPECT_FALSE(wrong_version);
    EXPECT_EQ(wrong_version.error().code(), snt::core::ErrorCode::kProtocolError);
}

TEST(ReplicationServiceTest, OrdersInboundBeforeOutboundEmission) {
    FakeReplicationTransport transport;
    transport.peers = {7, 8};
    transport.events = {
        {.kind = snt::network::ReplicationEventKind::PeerConnected, .peer = 7},
        {.kind = snt::network::ReplicationEventKind::FrameReceived,
         .peer = 7,
         .frame = {.payload = bytes_from_text("inbound")}},
    };
    RecordingReplicationHandler handler;
    snt::network::ReplicationService service(transport, handler);
    const snt::network::ReplicationTickContext context{.tick_index = 9, .delta_seconds = 0.05f};

    ASSERT_TRUE(service.poll_inbound(context));
    ASSERT_TRUE(service.emit_outbound(context));
    ASSERT_EQ(handler.calls, (std::vector<std::string>{"connected:7", "frame:7:inbound", "emit"}));
    ASSERT_EQ(transport.sent.size(), 2u);
    EXPECT_EQ(transport.sent[0].first, 7u);
    EXPECT_EQ(transport.sent[1].first, 8u);
    EXPECT_EQ(text_from_bytes(transport.sent[0].second.payload), "outbound");

    service.shutdown();
    EXPECT_TRUE(transport.shutdown_called);
}

TEST(ReplicationServiceTest, LetsHandlerDisconnectAPeerDuringOutbound) {
    FakeReplicationTransport transport;
    transport.peers = {7};
    RecordingReplicationHandler handler;
    handler.disconnect_during_emit = true;
    handler.disconnect_peer = 7;
    snt::network::ReplicationService service(transport, handler);

    ASSERT_TRUE(service.emit_outbound({.tick_index = 9, .delta_seconds = 0.05f}));
    ASSERT_EQ(handler.calls, (std::vector<std::string>{"emit"}));
    ASSERT_EQ(transport.disconnected.size(), 1u);
    EXPECT_EQ(transport.disconnected.front().first, 7u);
    EXPECT_EQ(transport.disconnected.front().second, "test outbound disconnect");
}

TEST(ReplicationServiceTest, SkipsLaterEventsForAPeerRejectedDuringConnection) {
    FakeReplicationTransport transport;
    transport.peers = {7};
    transport.events = {
        {.kind = snt::network::ReplicationEventKind::PeerConnected, .peer = 7},
        {.kind = snt::network::ReplicationEventKind::FrameReceived,
         .peer = 7,
         .frame = {.payload = bytes_from_text("must-not-reach-handler")}},
    };
    RecordingReplicationHandler handler;
    handler.reject_connections = true;
    snt::network::ReplicationService service(transport, handler);

    ASSERT_TRUE(service.poll_inbound({.tick_index = 1, .delta_seconds = 0.05f}));
    EXPECT_EQ(handler.calls, (std::vector<std::string>{"connected:7", "disconnected:7"}));
    ASSERT_EQ(transport.disconnected.size(), 1u);
    EXPECT_EQ(transport.disconnected.front().first, 7u);
}

TEST(SteamP2PReplicationTransportTest, EncodesAndDecodesThroughInjectedBackend) {
    FakeSteamBackend backend;
    auto transport_result = snt::network::SteamP2PReplicationTransport::create(backend);
    ASSERT_TRUE(transport_result) << transport_result.error().format();
    auto transport = std::move(*transport_result);
    constexpr snt::network::PeerId kPeer = 4455;
    backend.events.push_back({
        .kind = snt::network::SteamP2PBackendEventKind::PeerConnected,
        .peer = kPeer,
    });
    auto connected = transport->poll();
    ASSERT_TRUE(connected) << connected.error().format();
    ASSERT_EQ(connected->size(), 1u);

    snt::network::ReplicationFrame outbound;
    outbound.server_tick = 22;
    outbound.payload = bytes_from_text("steam-out");
    ASSERT_TRUE(transport->send(kPeer, outbound));
    ASSERT_EQ(backend.sent.size(), 1u);
    EXPECT_EQ(backend.sent.front().channel, snt::network::ReplicationChannel::Reliable);

    snt::network::detail::WirePacket inbound;
    inbound.kind = snt::network::detail::WirePacketKind::Application;
    inbound.channel = snt::network::ReplicationChannel::Unreliable;
    inbound.server_tick = 23;
    inbound.payload = bytes_from_text("steam-in");
    auto encoded = snt::network::detail::encode_wire_packet(inbound, {});
    ASSERT_TRUE(encoded) << encoded.error().format();
    backend.events.push_back({
        .kind = snt::network::SteamP2PBackendEventKind::PacketReceived,
        .peer = kPeer,
        .channel = snt::network::ReplicationChannel::Unreliable,
        .bytes = std::move(*encoded),
    });
    auto received = transport->poll();
    ASSERT_TRUE(received) << received.error().format();
    ASSERT_EQ(received->size(), 1u);
    EXPECT_EQ(received->front().kind, snt::network::ReplicationEventKind::FrameReceived);
    EXPECT_EQ(received->front().frame.server_tick, 23u);
    EXPECT_EQ(text_from_bytes(received->front().frame.payload), "steam-in");
}

}  // namespace
