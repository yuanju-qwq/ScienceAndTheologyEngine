#pragma once

#include "core/expected.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snt::assets {

class TextureCache;

struct MaterialAtlasImage {
    uint32_t tile_size = 32;
    uint32_t columns = 1;
    uint32_t rows = 1;
    uint32_t width = 32;
    uint32_t height = 32;
    std::vector<uint8_t> rgba;
};

snt::core::Expected<MaterialAtlasImage> build_material_atlas(
    TextureCache& texture_cache,
    const std::vector<std::string>& tile_paths,
    uint32_t tile_size);

snt::core::Expected<MaterialAtlasImage> build_default_voxel_material_atlas(
    TextureCache& texture_cache);

}  // namespace snt::assets
