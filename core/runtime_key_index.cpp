// C++ facade for the Zig-owned deterministic StringKey-to-runtime-ID index.

#define SNT_LOG_CHANNEL "runtime_key_index"
#include "runtime_key_index.h"

#include <limits>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/log.h"

namespace snt::core {
namespace {

SntAbiByteView byte_view(std::string_view value) noexcept {
    return {reinterpret_cast<const uint8_t*>(value.data()),
            static_cast<uint64_t>(value.size())};
}

ErrorCode error_code_for_status(SntAbiStatus status) noexcept {
    switch (status) {
    case SNT_ABI_STATUS_INVALID_ARGUMENT:
    case SNT_ABI_STATUS_INCOMPATIBLE_VERSION:
        return ErrorCode::kInvalidArgument;
    case SNT_ABI_STATUS_INVALID_STATE:
    case SNT_ABI_STATUS_NOT_READY:
        return ErrorCode::kInvalidState;
    default:
        return ErrorCode::kUnknown;
    }
}

void forward_abi_log(void*,
                     SntAbiLogSeverity severity,
                     const char* channel,
                     const char* message) noexcept {
    try {
        LogLevel level = LogLevel::kError;
        switch (severity) {
        case SNT_ABI_LOG_TRACE:
            level = LogLevel::kTrace;
            break;
        case SNT_ABI_LOG_DEBUG:
            level = LogLevel::kDebug;
            break;
        case SNT_ABI_LOG_INFO:
            level = LogLevel::kInfo;
            break;
        case SNT_ABI_LOG_WARN:
            level = LogLevel::kWarn;
            break;
        case SNT_ABI_LOG_ERROR:
            level = LogLevel::kError;
            break;
        case SNT_ABI_LOG_FATAL:
            level = LogLevel::kFatal;
            break;
        default:
            break;
        }
        Logger::instance().log(level,
                               channel != nullptr ? channel : SNT_LOG_CHANNEL,
                               "%s",
                               message != nullptr ? message : "Zig runtime key-index diagnostic");
    } catch (...) {
        // No C++ exception may cross the Zig C callback boundary.
    }
}

void log_snapshot_retain_failure(SntAbiStatus status) noexcept {
    SNT_LOG_ERROR("Zig snapshot retain failed: %s", snt_abi_status_message(status));
}

}  // namespace

RuntimeKeyIndex::Snapshot::Snapshot(const Snapshot& other) noexcept {
    if (other.handle_ == nullptr) return;
    const SntAbiStatus status = snt_runtime_key_index_snapshot_retain(other.handle_);
    if (status == SNT_ABI_STATUS_OK) {
        handle_ = other.handle_;
    } else {
        log_snapshot_retain_failure(status);
    }
}

RuntimeKeyIndex::Snapshot& RuntimeKeyIndex::Snapshot::operator=(const Snapshot& other) noexcept {
    if (this == &other) return *this;
    if (other.handle_ != nullptr) {
        const SntAbiStatus status = snt_runtime_key_index_snapshot_retain(other.handle_);
        if (status != SNT_ABI_STATUS_OK) {
            log_snapshot_retain_failure(status);
            return *this;
        }
    }
    if (handle_ != nullptr) snt_runtime_key_index_snapshot_release(handle_);
    handle_ = other.handle_;
    return *this;
}

RuntimeKeyIndex::Snapshot::Snapshot(Snapshot&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

RuntimeKeyIndex::Snapshot& RuntimeKeyIndex::Snapshot::operator=(Snapshot&& other) noexcept {
    if (this == &other) return *this;
    if (handle_ != nullptr) snt_runtime_key_index_snapshot_release(handle_);
    handle_ = std::exchange(other.handle_, nullptr);
    return *this;
}

RuntimeKeyIndex::Snapshot::~Snapshot() {
    if (handle_ != nullptr) snt_runtime_key_index_snapshot_release(handle_);
}

std::optional<RuntimeKeyId> RuntimeKeyIndex::Snapshot::find_id(
    std::string_view key) const noexcept {
    if (handle_ == nullptr) return std::nullopt;
    RuntimeKeyId id = kInvalidRuntimeKeyId;
    if (snt_runtime_key_index_snapshot_find_id(handle_, byte_view(key), &id) !=
            SNT_ABI_STATUS_OK ||
        id == kInvalidRuntimeKeyId) {
        return std::nullopt;
    }
    return id;
}

std::optional<std::string_view> RuntimeKeyIndex::Snapshot::find_key(
    RuntimeKeyId id) const noexcept {
    if (handle_ == nullptr) return std::nullopt;
    SntAbiByteView key{nullptr, 0u};
    if (snt_runtime_key_index_snapshot_find_key(handle_, id, &key) != SNT_ABI_STATUS_OK ||
        key.data == nullptr) {
        return std::nullopt;
    }
    return std::string_view{reinterpret_cast<const char*>(key.data),
                            static_cast<size_t>(key.size_bytes)};
}

uint64_t RuntimeKeyIndex::Snapshot::generation() const noexcept {
    if (handle_ == nullptr) return 0;
    SntRuntimeKeyIndexSnapshotInfo info = SNT_RUNTIME_KEY_INDEX_SNAPSHOT_INFO_INIT;
    return snt_runtime_key_index_snapshot_query(handle_, &info) == SNT_ABI_STATUS_OK
               ? info.generation
               : 0;
}

size_t RuntimeKeyIndex::Snapshot::size() const noexcept {
    if (handle_ == nullptr) return 0;
    SntRuntimeKeyIndexSnapshotInfo info = SNT_RUNTIME_KEY_INDEX_SNAPSHOT_INFO_INIT;
    if (snt_runtime_key_index_snapshot_query(handle_, &info) != SNT_ABI_STATUS_OK ||
        info.key_count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return 0;
    }
    return static_cast<size_t>(info.key_count);
}

RuntimeKeyIndex::RuntimeKeyIndex() {
    SntRuntimeKeyIndexCreateInfo create_info = SNT_RUNTIME_KEY_INDEX_CREATE_INFO_INIT;
    create_info.log = forward_abi_log;
    const SntAbiStatus status = snt_runtime_key_index_create(&create_info, &handle_);
    if (status != SNT_ABI_STATUS_OK || handle_ == nullptr) {
        handle_ = nullptr;
        SNT_LOG_ERROR("Zig runtime key-index create failed: %s", snt_abi_status_message(status));
    }
}

RuntimeKeyIndex::~RuntimeKeyIndex() {
    if (handle_ != nullptr) snt_runtime_key_index_destroy(handle_);
}

Expected<void> RuntimeKeyIndex::rebuild(std::span<const std::string_view> keys) {
    if (handle_ == nullptr) {
        return Error{ErrorCode::kInvalidState, "Runtime key index is not initialized"};
    }

    std::vector<SntAbiByteView> abi_keys;
    abi_keys.reserve(keys.size());
    for (const std::string_view key : keys) {
        abi_keys.push_back(byte_view(key));
    }

    const SntAbiStatus status = snt_runtime_key_index_rebuild(
        handle_, abi_keys.empty() ? nullptr : abi_keys.data(), static_cast<uint64_t>(abi_keys.size()));
    if (status == SNT_ABI_STATUS_OK) return {};

    return Error{error_code_for_status(status), snt_abi_status_message(status)};
}

RuntimeKeyIndex::Snapshot RuntimeKeyIndex::snapshot() const noexcept {
    if (handle_ == nullptr) return {};
    SntRuntimeKeyIndexSnapshot* snapshot_handle = nullptr;
    if (snt_runtime_key_index_acquire_snapshot(handle_, &snapshot_handle) != SNT_ABI_STATUS_OK ||
        snapshot_handle == nullptr) {
        return {};
    }
    return Snapshot(snapshot_handle);
}

std::optional<RuntimeKeyId> RuntimeKeyIndex::find_id(std::string_view key) const noexcept {
    return snapshot().find_id(key);
}

std::optional<std::string_view> RuntimeKeyIndex::find_key(RuntimeKeyId id) const noexcept {
    return snapshot().find_key(id);
}

uint64_t RuntimeKeyIndex::generation() const noexcept {
    return snapshot().generation();
}

size_t RuntimeKeyIndex::size() const noexcept {
    return snapshot().size();
}

void RuntimeKeyIndex::restore(const Snapshot& snapshot) noexcept {
    if (handle_ == nullptr || snapshot.handle_ == nullptr) return;
    const SntAbiStatus status =
        snt_runtime_key_index_restore_snapshot(handle_, snapshot.handle_);
    if (status != SNT_ABI_STATUS_OK) {
        SNT_LOG_ERROR("Zig runtime key-index restore failed: %s", snt_abi_status_message(status));
    }
}

}  // namespace snt::core
