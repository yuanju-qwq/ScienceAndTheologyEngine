// Type-safe handle for assets in the AssetCache.
//
// Design:
//   - Wraps a uint32_t ID; 0xFFFFFFFF is the invalid sentinel.
//   - Tagged with a phantom type `Tag` so AssetHandle<MeshTag> and
//     AssetHandle<TextureTag> don't silently mix (compile-time safety).
//   - Trivially copyable — safe to store in ECS components and pass by
//     value across module boundaries.
//
// Rationale: replaces the previously duplicated `MeshHandle` types in
// ecs/components.h and render/mesh_cache.h. One canonical handle type
// eliminates the layout-duplication bridge between layers.

#pragma once

#include <cstdint>
#include <functional>  // std::hash

namespace snt::assets {

// Generic handle. `Tag` is a phantom type (never constructed) used only
// to differentiate handle types at compile time.
template <typename Tag>
struct AssetHandle {
    static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;

    uint32_t id = kInvalidId;

    constexpr bool valid() const { return id != kInvalidId; }

    friend bool operator==(const AssetHandle&, const AssetHandle&) = default;
    friend bool operator!=(const AssetHandle&, const AssetHandle&) = default;
};

// ---------------------------------------------------------------------------
// Phantom tags for built-in asset types
// ---------------------------------------------------------------------------
// Add new tags here as new resource kinds land (Texture, Material, Shader,
// Audio, ...). Each tag pairs with an AssetCache<T> registered in
// AssetManager.
struct MeshAssetTag {};
struct TextureAssetTag {};    // reserved for P3
struct MaterialAssetTag {};   // reserved for P3

using MeshHandle     = AssetHandle<MeshAssetTag>;
using TextureHandle  = AssetHandle<TextureAssetTag>;
using MaterialHandle = AssetHandle<MaterialAssetTag>;

}  // namespace snt::assets

// std::hash specialization so AssetHandle can be used as a key in
// unordered_map / unordered_set.
template <typename Tag>
struct std::hash<snt::assets::AssetHandle<Tag>> {
    size_t operator()(const snt::assets::AssetHandle<Tag>& h) const noexcept {
        return std::hash<uint32_t>{}(h.id);
    }
};
