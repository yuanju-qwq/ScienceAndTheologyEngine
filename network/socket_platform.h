// Internal socket-platform boundary for direct TCP+UDP transport and LAN discovery.
//
// This header deliberately stays private to snt_network. It isolates
// Winsock/POSIX spelling differences so public replication contracts never
// expose native handles or platform headers.

#pragma once

#include "core/expected.h"

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace snt::network::detail {

using Socket = SOCKET;
using SocketLength = int;
inline constexpr Socket kInvalidSocket = INVALID_SOCKET;
inline constexpr int kSocketError = SOCKET_ERROR;

}  // namespace snt::network::detail
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace snt::network::detail {

using Socket = int;
using SocketLength = socklen_t;
inline constexpr Socket kInvalidSocket = -1;
inline constexpr int kSocketError = -1;

}  // namespace snt::network::detail
#endif

namespace snt::network::detail {

[[nodiscard]] snt::core::Expected<void> initialize_socket_platform();
void shutdown_socket_platform() noexcept;

[[nodiscard]] int last_socket_error() noexcept;
[[nodiscard]] bool socket_would_block(int error) noexcept;
[[nodiscard]] bool socket_connect_in_progress(int error) noexcept;
[[nodiscard]] bool socket_connection_lost(int error) noexcept;
[[nodiscard]] std::string socket_error_message(int error);

void close_socket(Socket socket) noexcept;
[[nodiscard]] bool set_socket_non_blocking(Socket socket) noexcept;
[[nodiscard]] bool set_socket_reuse_address(Socket socket) noexcept;
[[nodiscard]] bool set_socket_broadcast(Socket socket) noexcept;
[[nodiscard]] bool set_socket_tcp_no_delay(Socket socket) noexcept;

}  // namespace snt::network::detail
