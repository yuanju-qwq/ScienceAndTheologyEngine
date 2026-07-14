// Type-safe handles for asset reference registries.
//
// Design:
//   - Wraps a uint32_t ID; 0xFFFFFFFF is the invalid sentinel.
//   - Tagged with a phantom type `Tag` so AssetHandle<MeshTag> and
//     AssetHandle<TextureTag> don't silently mix (compile-time safety).
//   - Trivially copyable — safe to store in ECS components and pass by
//     value across module boundaries.
//
// Rationale: replaces previously duplicated mesh-handle layouts with one
// canonical value type shared by render components, scene references, and
// AssetManager's GPU-residency map.

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
// Audio, ...). Each tag receives a scene/reference registry and GPU-residency
// policy when its uploader support is implemented.
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
