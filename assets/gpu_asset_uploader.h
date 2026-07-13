// GPU asset residency boundary: turns owned source data into device resources.
//
// Ownership/lifecycle:
//   - IGpuAssetUploader is owned by the render-device lifetime. Every returned
//     GpuAssetResidencyToken becomes invalid when its uploader/device stops.
//   - All methods are render-thread/device-affine. Callers must synchronize
//     before release() or evict_unused() can reclaim in-flight GPU resources.
//   - This interface receives no World and provides no global-service access;
//     simulation and loading workers communicate through explicit values only.
//
// This declaration deliberately contains no Vulkan type. The current
// AssetManager/VulkanMeshLoader implementation is not yet migrated here.

#pragma once

#include "assets/asset_source.h"
#include "core/expected.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace snt::assets {

// The device-side resource category requested by the asset pipeline. Actual
// source decoding remains implementation-defined during the staged migration.
enum class GpuAssetKind : std::uint8_t {
    kUnknown = 0,
    kMesh,
    kTexture,
    kShader,
    kFontAtlas,
};

// Value request passed across the CPU-to-GPU boundary. Taking this type by
// value allows the uploader to retain or move the source payload until its
// device-side staging work has completed.
struct GpuAssetUploadRequest {
    GpuAssetKind kind = GpuAssetKind::kUnknown;
    AssetSourceData source;
};

// Opaque, uploader-local residency identity. It carries no Vulkan handle and
// must only be supplied back to the uploader that created it.
struct GpuAssetResidencyToken {
    static constexpr std::uint64_t kInvalidValue =
        std::numeric_limits<std::uint64_t>::max();

    std::uint64_t value = kInvalidValue;

    [[nodiscard]] constexpr bool valid() const {
        return value != kInvalidValue;
    }

    friend constexpr bool operator==(const GpuAssetResidencyToken&,
                                     const GpuAssetResidencyToken&) = default;
};

class IGpuAssetUploader {
public:
    virtual ~IGpuAssetUploader() = default;

    IGpuAssetUploader(const IGpuAssetUploader&) = delete;
    IGpuAssetUploader& operator=(const IGpuAssetUploader&) = delete;

    // Upload or locate a device-resident asset. The request is intentionally
    // by value: implementations may retain its bytes for asynchronous staging.
    [[nodiscard]] virtual snt::core::Expected<GpuAssetResidencyToken> upload(
        GpuAssetUploadRequest request) = 0;

    // Relinquish this caller's token. Physical destruction may be deferred
    // until GPU fences and cache policy permit it.
    [[nodiscard]] virtual snt::core::Expected<void> release(
        GpuAssetResidencyToken token) = 0;

    // Reclaim every released, no-longer-in-flight resource. Returns the count
    // of residency entries reclaimed; implementations may retain active data.
    [[nodiscard]] virtual snt::core::Expected<std::size_t> evict_unused() = 0;

protected:
    IGpuAssetUploader() = default;
};

}  // namespace snt::assets
