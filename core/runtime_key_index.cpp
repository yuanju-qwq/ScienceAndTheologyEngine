// Deterministic StringKey-to-runtime-ID index implementation.

#include "runtime_key_index.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/error.h"

namespace snt::core {

struct RuntimeKeyIndex::Data {
    // IDs are assigned from a sorted vector during rebuild. Lookup itself is
    // an average O(1) boundary operation; workers retain the resulting ID.
    std::unordered_map<std::string, RuntimeKeyId> ids_by_key;
    // Index zero is the invalid-ID sentinel. Every valid ID can therefore be
    // used directly as an array index for reverse lookup.
    std::vector<std::string> keys_by_id{std::string{}};
    uint64_t generation = 0;
};

RuntimeKeyIndex::RuntimeKeyIndex() : data_(std::make_shared<Data>()) {}

std::optional<RuntimeKeyId> RuntimeKeyIndex::Snapshot::find_id(
    std::string_view key) const noexcept {
    if (!data_) return std::nullopt;
    const auto found = data_->ids_by_key.find(std::string{key});
    if (found == data_->ids_by_key.end()) return std::nullopt;
    return found->second;
}

std::optional<std::string_view> RuntimeKeyIndex::Snapshot::find_key(
    RuntimeKeyId id) const noexcept {
    if (!data_ || id == kInvalidRuntimeKeyId ||
        id >= data_->keys_by_id.size()) {
        return std::nullopt;
    }
    return data_->keys_by_id[id];
}

uint64_t RuntimeKeyIndex::Snapshot::generation() const noexcept {
    return data_ ? data_->generation : 0;
}

size_t RuntimeKeyIndex::Snapshot::size() const noexcept {
    return data_ ? data_->ids_by_key.size() : 0;
}

Expected<void> RuntimeKeyIndex::rebuild(std::span<const std::string_view> keys) {
    if (keys.size() > static_cast<size_t>(std::numeric_limits<RuntimeKeyId>::max())) {
        return Error{ErrorCode::kInvalidArgument,
                     "Runtime key index contains more keys than its ID domain"};
    }
    if (data_ && data_->generation == std::numeric_limits<uint64_t>::max()) {
        return Error{ErrorCode::kInvalidState,
                     "Runtime key index generation is exhausted"};
    }

    std::vector<std::string> sorted_keys;
    sorted_keys.reserve(keys.size());
    for (const std::string_view key : keys) {
        if (key.empty() || key.find('\0') != std::string_view::npos) {
            return Error{ErrorCode::kInvalidArgument,
                         "Runtime key index keys must be non-empty and contain no null character"};
        }
        sorted_keys.emplace_back(key);
    }
    std::sort(sorted_keys.begin(), sorted_keys.end());
    const auto duplicate = std::adjacent_find(sorted_keys.begin(), sorted_keys.end());
    if (duplicate != sorted_keys.end()) {
        return Error{ErrorCode::kInvalidArgument,
                     "Runtime key index keys must be unique"};
    }

    // Build all owned storage before replacing data_. Any validation or
    // allocation failure above leaves the old immutable snapshot available.
    auto candidate = std::make_shared<Data>();
    candidate->keys_by_id.reserve(sorted_keys.size() + 1);
    for (const std::string& key : sorted_keys) {
        const auto id = static_cast<RuntimeKeyId>(candidate->keys_by_id.size());
        candidate->ids_by_key.emplace(key, id);
        candidate->keys_by_id.push_back(key);
    }
    candidate->generation = data_ ? data_->generation + 1 : 1;
    data_ = std::move(candidate);
    return {};
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

void RuntimeKeyIndex::restore(Snapshot snapshot) noexcept {
    if (snapshot.data_) data_ = std::move(snapshot.data_);
}

}  // namespace snt::core
