// C++ facade for Zig-owned immutable runtime key-index snapshots.
//
// Persistent content stores semantic string keys. Runtime systems can capture
// a Snapshot and use its compact contiguous IDs without observing a later
// content reload. The data, sorted-ID assignment, and snapshot ownership live
// in the language-neutral Zig ABI; this facade only preserves ergonomic C++
// values for existing C++ engine callers.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "abi/runtime_key_index_abi.h"
#include "core/expected.h"

namespace snt::core {

using RuntimeKeyId = SntRuntimeKeyId;
inline constexpr RuntimeKeyId kInvalidRuntimeKeyId = SNT_RUNTIME_KEY_ID_INVALID;

class RuntimeKeyIndex final {
public:
    // A value-copyable immutable index view. Retain it across worker work or
    // asynchronous callbacks to keep the key/ID generation coherent.
    class Snapshot final {
    public:
        Snapshot() = default;
        Snapshot(const Snapshot& other) noexcept;
        Snapshot& operator=(const Snapshot& other) noexcept;
        Snapshot(Snapshot&& other) noexcept;
        Snapshot& operator=(Snapshot&& other) noexcept;
        ~Snapshot();

        [[nodiscard]] std::optional<RuntimeKeyId> find_id(
            std::string_view key) const noexcept;
        [[nodiscard]] std::optional<std::string_view> find_key(
            RuntimeKeyId id) const noexcept;
        [[nodiscard]] uint64_t generation() const noexcept;
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    private:
        friend class RuntimeKeyIndex;

        explicit Snapshot(SntRuntimeKeyIndexSnapshot* handle) noexcept
            : handle_(handle) {}

        SntRuntimeKeyIndexSnapshot* handle_ = nullptr;
    };

    RuntimeKeyIndex();
    ~RuntimeKeyIndex();

    RuntimeKeyIndex(const RuntimeKeyIndex&) = delete;
    RuntimeKeyIndex& operator=(const RuntimeKeyIndex&) = delete;
    RuntimeKeyIndex(RuntimeKeyIndex&&) = delete;
    RuntimeKeyIndex& operator=(RuntimeKeyIndex&&) = delete;

    // Keys must already be canonical for their owning content domain. The
    // index validates only generic invariants, sorts lexicographically, and
    // assigns IDs [1, N]; ID 0 always remains invalid. Zig stores the sorted
    // immutable bytes and performs deterministic binary-search lookup, while
    // workers retain only the resulting fixed-width ID.
    [[nodiscard]] Expected<void> rebuild(std::span<const std::string_view> keys);

    [[nodiscard]] Snapshot snapshot() const noexcept;
    [[nodiscard]] std::optional<RuntimeKeyId> find_id(std::string_view key) const noexcept;
    [[nodiscard]] std::optional<std::string_view> find_key(RuntimeKeyId id) const noexcept;
    [[nodiscard]] uint64_t generation() const noexcept;
    [[nodiscard]] size_t size() const noexcept;

    // Restores a previously captured snapshot. Content registries use this
    // only when rolling back a failed transactional reload.
    void restore(const Snapshot& snapshot) noexcept;

private:
    SntRuntimeKeyIndex* handle_ = nullptr;
};

}  // namespace snt::core
