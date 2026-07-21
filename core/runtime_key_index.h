// Immutable snapshots for deterministic runtime identifiers.
//
// Persistent content stores semantic string keys. Runtime systems can capture
// a Snapshot and use its compact contiguous IDs without observing a later
// content reload. Rebuilds construct a complete replacement before publishing
// it, so a failed rebuild leaves the current snapshot untouched.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "core/expected.h"

namespace snt::core {

using RuntimeKeyId = uint32_t;
inline constexpr RuntimeKeyId kInvalidRuntimeKeyId = 0;

class RuntimeKeyIndex final {
private:
    struct Data;

public:
    // A value-copyable immutable index view. Retain it across worker work or
    // asynchronous callbacks to keep the key/ID generation coherent.
    class Snapshot final {
    public:
        Snapshot() = default;

        [[nodiscard]] std::optional<RuntimeKeyId> find_id(
            std::string_view key) const noexcept;
        [[nodiscard]] std::optional<std::string_view> find_key(
            RuntimeKeyId id) const noexcept;
        [[nodiscard]] uint64_t generation() const noexcept;
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    private:
        friend class RuntimeKeyIndex;

        explicit Snapshot(std::shared_ptr<const Data> data) noexcept
            : data_(std::move(data)) {}

        std::shared_ptr<const Data> data_;
    };

    RuntimeKeyIndex();

    RuntimeKeyIndex(const RuntimeKeyIndex&) = delete;
    RuntimeKeyIndex& operator=(const RuntimeKeyIndex&) = delete;

    // Keys must already be canonical for their owning content domain. The
    // index validates only generic invariants, sorts lexicographically, and
    // assigns IDs [1, N]; ID 0 always remains invalid. String-to-ID lookup is
    // average O(1), while workers retain only the resulting fixed-width ID.
    [[nodiscard]] Expected<void> rebuild(std::span<const std::string_view> keys);

    [[nodiscard]] Snapshot snapshot() const noexcept { return Snapshot(data_); }
    [[nodiscard]] std::optional<RuntimeKeyId> find_id(std::string_view key) const noexcept;
    [[nodiscard]] std::optional<std::string_view> find_key(RuntimeKeyId id) const noexcept;
    [[nodiscard]] uint64_t generation() const noexcept;
    [[nodiscard]] size_t size() const noexcept;

    // Restores a previously captured snapshot. Content registries use this
    // only when rolling back a failed transactional reload.
    void restore(Snapshot snapshot) noexcept;

private:
    std::shared_ptr<const Data> data_;
};

}  // namespace snt::core
