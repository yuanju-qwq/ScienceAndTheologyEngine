// Asset source boundary: reads owned asset bytes without renderer access.
//
// Ownership/lifecycle:
//   - IAssetSource implementations are owned and injected by the runtime or
//     the host. Returned AssetSourceData owns both its canonical identity and
//     bytes; callers never retain source-owned buffers.
//   - read() may run on a loading worker rather than the render thread.
//     Implementations must document their own synchronization and must not
//     call GPU APIs, mutate World, or recover a service through global state.
//
// FilesystemAssetSource implements this reusable source boundary. AssetManager
// still owns the current path-to-VulkanMesh flow until catalog/source migration
// is scheduled.

#pragma once

#include "core/expected.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snt::assets {

// A catalog-facing lookup key. The source maps it to its canonical path or
// URI, so callers do not need to know whether content comes from a directory,
// package, cache, or remote source.
struct AssetSourceRequest {
    std::string requested_path;
};

// Owned-at-the-boundary source data. `canonical_path` is the stable,
// normalized path or URI chosen by the source; it is suitable for cache keys
// and diagnostics. `bytes` is owned by the caller after a successful read().
struct AssetSourceData {
    std::string canonical_path;
    std::vector<std::uint8_t> bytes;
};

class IAssetSource {
public:
    virtual ~IAssetSource() = default;

    IAssetSource(const IAssetSource&) = delete;
    IAssetSource& operator=(const IAssetSource&) = delete;
    IAssetSource(IAssetSource&&) = default;
    IAssetSource& operator=(IAssetSource&&) = default;

    // Resolve and read one asset. The request is passed by value so an
    // asynchronous source can retain it without borrowing caller memory.
    [[nodiscard]] virtual snt::core::Expected<AssetSourceData> read(
        AssetSourceRequest request) = 0;

protected:
    IAssetSource() = default;
};

}  // namespace snt::assets
