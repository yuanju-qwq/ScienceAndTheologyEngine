// Demo world bootstrap.
//
// Development-only terrain setup used to keep the standalone engine visually
// testable while the real world/session loader is still being built. Keeping
// this in its own module prevents Engine::init from becoming the place where
// gameplay content is hardcoded.

#pragma once

#include "core/expected.h"

#include <cstdint>

namespace snt::data { class ChunkRegistry; }
namespace snt::voxel { class ChunkRenderSystem; }

namespace snt::engine {

struct DemoWorldBootstrapDesc {
    bool enabled = true;
    uint32_t seed = 20240601u;
};

snt::core::Expected<void> bootstrap_demo_world(
    const DemoWorldBootstrapDesc& desc,
    snt::data::ChunkRegistry& chunk_registry,
    snt::voxel::ChunkRenderSystem& chunk_render_system);

}  // namespace snt::engine
