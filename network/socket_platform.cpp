// Native socket helpers used only by snt_network.

#include "network/socket_platform.h"

#include "core/error.h"

#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#endif

namespace snt::network::detail {

snt::core::Expected<void> initialize_socket_platform() {
#if defined(_WIN32)
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        return snt::core::Error{snt::core::ErrorCode::kNetworkInitFailed,
                                "WSAStartup failed: " + socket_error_message(result)};
    }
#endif
    return {};
}

void shutdown_socket_platform() noexcept {
#if defined(_WIN32)
    WSACleanup();
#endif
}

int last_socket_error() noexcept {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool socket_would_block(int error) noexcept {
#if defined(_WIN32)
    return error == WSAEWOULDBLOCK;
#else
    return error == EWOULDBLOCK || error == EAGAIN;
#endif
}

bool socket_connect_in_progress(int error) noexcept {
#if defined(_WIN32)
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
#else
    return error == EINPROGRESS || error == EALREADY || socket_would_block(error);
#endif
}

bool socket_connection_lost(int error) noexcept {
#if defined(_WIN32)
    return error == WSAECONNRESET || error == WSAECONNABORTED || error == WSAENOTCONN;
#else
    return error == ECONNRESET || error == EPIPE || error == ENOTCONN;
#endif
}

std::string socket_error_message(int error) {
#if defined(_WIN32)
    char* message = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(error), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message), 0, nullptr);
    if (length == 0 || message == nullptr) {
        return "Winsock error " + std::to_string(error);
    }

    std::string result(message, length);
    LocalFree(message);
    while (!result.empty() &&
           (result.back() == '\r' || result.back() == '\n' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
#else
    return std::strerror(error);
#endif
}

void close_socket(Socket socket) noexcept {
    if (socket == kInvalidSocket) return;
#if defined(_WIN32)
    closesocket(socket);
#else
    ::close(socket);
#endif
}

bool set_socket_non_blocking(Socket socket) noexcept {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool set_socket_reuse_address(Socket socket) noexcept {
#if defined(_WIN32)
    const BOOL enabled = TRUE;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                      reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0;
#else
    const int enabled = 1;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == 0;
#endif
}

bool set_socket_broadcast(Socket socket) noexcept {
#if defined(_WIN32)
    const BOOL enabled = TRUE;
    return setsockopt(socket, SOL_SOCKET, SO_BROADCAST,
                      reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0;
#else
    const int enabled = 1;
    return setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) == 0;
#endif
}

bool set_socket_tcp_no_delay(Socket socket) noexcept {
#if defined(_WIN32)
    const BOOL enabled = TRUE;
    return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0;
#else
    const int enabled = 1;
    return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == 0;
#endif
}

}  // namespace snt::network::detail
