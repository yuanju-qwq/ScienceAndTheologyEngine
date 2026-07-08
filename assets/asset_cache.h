// Generic deduplicating asset cache.
//
// Design:
//   - load(path) returns an AssetHandle<Tag>. Loading the same path twice
//     returns the same handle (deduplication via path_to_handle_ map).
//   - get(handle) returns T* (or nullptr for invalid handle).
//   - Lifetime: the cache owns all T* objects; destroy() releases them.
//   - Loading + destruction are delegated to caller-provided functions
//     (LoaderFn / DestroyFn), so the cache is resource-agnostic.
//   - `Tag` is decoupled from `T` so handles can be lightweight phantom
//     types (e.g. MeshAssetTag) that don't drag in the asset's full
//     definition. Default Tag=T lets tests omit the second parameter.
//
// Rationale: replaces the ad-hoc MeshCache with a reusable template.
// Adding a new asset type (Texture, Material) is now: define a Tag,
// define a Loader, register with AssetManager — no new cache code.

#pragma once

#include "assets/asset_handle.h"
#include "core/expected.h"
#include "core/hash.h"  // hash_string (path -> uint64_t key for path_to_handle_)

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace snt::assets {

template <typename T, typename Tag = T>
class AssetCache {
public:
    using LoaderFn  = std::function<snt::core::Expected<T*>(const std::string&)>;
    using DestroyFn = std::function<void(T*)>;

    AssetCache() = default;
    ~AssetCache() { destroy(); }

    AssetCache(const AssetCache&) = delete;
    AssetCache& operator=(const AssetCache&) = delete;

    // Wire up the loader + destroyer. Must be called before load().
    // `loader` returns a heap-owned T* on success (cache takes ownership)
    // or an Error. `destroyer` is called once per cached asset during
    // destroy(); it must release both the resource (e.g.
    // VulkanMesh::destroy()) and the heap memory (delete).
    snt::core::Expected<void> init(LoaderFn loader, DestroyFn destroyer) {
        if (!loader || !destroyer) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "AssetCache::init: null loader/destroyer"};
        }
        loader_   = std::move(loader);
        destroyer_ = std::move(destroyer);
        return {};
    }

    // Release all cached assets. Idempotent.
    void destroy() {
        if (destroyer_) {
            for (auto* p : slots_) {
                if (p) destroyer_(p);
            }
        }
        slots_.clear();
        path_to_handle_.clear();
        handle_to_path_.clear();
        loader_   = nullptr;
        destroyer_ = nullptr;
    }

    // Load a path. Dedup: same path -> same handle.
    //
    // Path lookup uses hash_string(path) as the unordered_map key (O(1)
    // uint64_t probe) instead of std::string comparison. The original
    // path string is preserved in handle_to_path_ for path_of() and for
    // error messages. See core/hash.h for collision-probability rationale
    // (vanishingly small for typical asset counts).
    snt::core::Expected<AssetHandle<Tag>> load(const std::string& path) {
        if (!loader_) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "AssetCache::load: not initialized"};
        }
        const uint64_t key = snt::core::hash_string(path);
        auto it = path_to_handle_.find(key);
        if (it != path_to_handle_.end()) {
            return it->second;
        }
        auto r = loader_(path);
        if (!r) {
            snt::core::Error e = r.error();
            e.with_context("AssetCache::load('" + path + "')");
            return e;
        }
        uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(*r);
        AssetHandle<Tag> h{id};
        path_to_handle_[key] = h;
        handle_to_path_[h] = path;  // reverse map for path_of()
        return h;
    }

    // Pre-allocate a handle for `path` WITHOUT loading the asset yet.
    // The handle id is assigned in call order (0, 1, 2, ...) so the same
    // sequence of register_preallocated() calls produces the same handles
    // across processes. Used by AssetManifest to give scene-referenced
    // assets stable ids that survive save/load cycles.
    //
    // The actual asset load happens lazily on the first get() call, or
    // eagerly when load_preallocated() is invoked. Until then, get(h)
    // returns nullptr (asset not yet on the GPU).
    //
    // Returns the pre-allocated handle. If `path` was already registered
    // (via register_preallocated or load), returns the existing handle.
    AssetHandle<Tag> register_preallocated(const std::string& path) {
        const uint64_t key = snt::core::hash_string(path);
        auto it = path_to_handle_.find(key);
        if (it != path_to_handle_.end()) {
            return it->second;
        }
        uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(nullptr);  // placeholder; real load deferred
        AssetHandle<Tag> h{id};
        path_to_handle_[key] = h;
        handle_to_path_[h] = path;
        return h;
    }

    // Eagerly load all pre-allocated (but not yet loaded) assets. Called
    // by AssetManager after manifest registration so the GPU is ready
    // before the first frame. Assets already loaded are skipped.
    snt::core::Expected<void> load_preallocated() {
        if (!loader_) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "AssetCache::load_preallocated: not initialized"};
        }
        for (size_t id = 0; id < slots_.size(); ++id) {
            if (slots_[id] != nullptr) continue;  // already loaded
            AssetHandle<Tag> h{static_cast<uint32_t>(id)};
            auto it = handle_to_path_.find(h);
            if (it == handle_to_path_.end()) continue;  // no path bound
            auto r = loader_(it->second);
            if (!r) {
                snt::core::Error e = r.error();
                e.with_context("AssetCache::load_preallocated('" + it->second + "')");
                return e;
            }
            slots_[id] = *r;
        }
        return {};
    }

    // Look up the path that produced a handle. Returns empty string if
    // the handle was never registered (e.g. invalid or runtime-only).
    // Used by scene serializers to write mesh paths instead of unstable
    // runtime handles.
    std::string path_of(AssetHandle<Tag> h) const {
        auto it = handle_to_path_.find(h);
        return it != handle_to_path_.end() ? it->second : std::string{};
    }

    // Look up by handle. Returns nullptr for invalid handle.
    T* get(AssetHandle<Tag> h) const {
        if (h.id == AssetHandle<Tag>::kInvalidId) return nullptr;
        if (h.id >= slots_.size()) return nullptr;
        return slots_[h.id];
    }

    // Convenience: same lookup but accepts a raw id (for callers like
    // ECS components that store the uint32_t directly).
    T* get(uint32_t id) const {
        return get(AssetHandle<Tag>{id});
    }

    // (P3) Reload a cached asset from disk. Stub for now.
    snt::core::Expected<void> reload(AssetHandle<Tag> h) {
        (void)h;
        return snt::core::Error{snt::core::ErrorCode::kNotImplemented,
                                "AssetCache::reload not implemented"};
    }

    size_t size() const { return slots_.size(); }

private:
    LoaderFn  loader_;
    DestroyFn destroyer_;
    std::vector<T*> slots_;
    // Primary lookup: hash_string(path) -> handle. uint64_t key gives O(1)
    // probes without per-lookup string hashing/comparison. Collisions are
    // astronomically unlikely (see core/hash.h); if one ever occurs it
    // would manifest as one path shadowing another (caught by asset load
    // errors when the wrong mesh appears).
    std::unordered_map<uint64_t, AssetHandle<Tag>> path_to_handle_;
    // Reverse lookup: handle -> original path string. Used by path_of()
    // for scene serialization + by load_preallocated() to re-resolve
    // paths for deferred loads. Stored as std::string (not hashed) so
    // the original path is preserved for error messages + disk writes.
    std::unordered_map<AssetHandle<Tag>, std::string> handle_to_path_;
};

}  // namespace snt::assets
