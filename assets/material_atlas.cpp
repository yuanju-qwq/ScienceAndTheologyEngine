#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"
#include "core/path_utils.h"

#include "assets/material_atlas.h"
#include "assets/texture_cache.h"

#include <algorithm>
#include <cmath>

namespace snt::assets {
namespace {

void copy_tile_resized_nearest(const TextureImage& src,
                               MaterialAtlasImage& atlas,
                               uint32_t tile) {
    const uint32_t tile_size = atlas.tile_size;
    for (uint32_t y = 0; y < tile_size; ++y) {
        const int sy = std::min(src.height - 1,
                                static_cast<int>((uint64_t(y) * src.height) / tile_size));
        for (uint32_t x = 0; x < tile_size; ++x) {
            const int sx = std::min(src.width - 1,
                                    static_cast<int>((uint64_t(x) * src.width) / tile_size));
            const size_t src_idx = (static_cast<size_t>(sy) * src.width + sx) * 4;
            const size_t dst_idx =
                (static_cast<size_t>(y) * atlas.width + tile * atlas.tile_size + x) * 4;
            atlas.rgba[dst_idx + 0] = src.rgba[src_idx + 0];
            atlas.rgba[dst_idx + 1] = src.rgba[src_idx + 1];
            atlas.rgba[dst_idx + 2] = src.rgba[src_idx + 2];
            atlas.rgba[dst_idx + 3] = src.rgba[src_idx + 3];
        }
    }
}

}  // namespace

snt::core::Expected<MaterialAtlasImage> build_material_atlas(
        TextureCache& texture_cache,
        const std::vector<std::string>& tile_paths,
        uint32_t tile_size) {
    if (tile_paths.empty() || tile_size == 0) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "build_material_atlas: empty tile list or zero tile size"};
    }

    MaterialAtlasImage atlas;
    atlas.tile_size = tile_size;
    atlas.columns = static_cast<uint32_t>(tile_paths.size());
    atlas.rows = 1;
    atlas.width = atlas.columns * tile_size;
    atlas.height = tile_size;
    atlas.rgba.assign(static_cast<size_t>(atlas.width) * atlas.height * 4, 255);

    for (uint32_t tile = 0; tile < tile_paths.size(); ++tile) {
        auto image = texture_cache.load_rgba(tile_paths[tile]);
        if (!image) {
            SNT_LOG_ERROR("Material atlas tile load failed: %s",
                          tile_paths[tile].c_str());
            return image.error().with_context(
                "build_material_atlas tile " + std::to_string(tile));
        }
        copy_tile_resized_nearest(**image, atlas, tile);
    }

    SNT_LOG_INFO("Material atlas built: %ux%u (%u tiles)",
                 atlas.width, atlas.height, atlas.columns * atlas.rows);
    return atlas;
}

snt::core::Expected<MaterialAtlasImage> build_default_voxel_material_atlas(
        TextureCache& texture_cache) {
    std::vector<std::string> paths = {
        snt::core::path_utils::resolve_game("assets/terrain/stone/stone_tile_32.png"),
        snt::core::path_utils::resolve_game("assets/terrain/dirt/dirt_tile_32.png"),
        snt::core::path_utils::resolve_game("assets/terrain/sand/sand_tile_01_32.png"),
        snt::core::path_utils::resolve_game("assets/terrain/snow/snow_tile_32.png"),
    };
    return build_material_atlas(texture_cache, paths, 32);
}

}  // namespace snt::assets
