// engine_test — standalone engine benchmark / smoke-test tool.
//
// Usage:
//   engine_test ecs_test            # 1000 entities, 1000 frames, report timing
//   engine_test ecs_test <n>        # n entities, 1000 frames
//   engine_test ecs_test <n> <frames>
//   engine_test data_test           # generate chunk (0,0,0), report non-air cells
//
// Measures:
//   1. Entity + component creation time (1000 entities x 3 components).
//   2. System update time (MovementSystem integrates Velocity into Position).
//   3. TerrainGenerator smoke test (chunk (0,0,0) non-air cell count).
//
// MovementSystem: a minimal system that iterates all entities with
// Position + Velocity and integrates velocity into position each tick.
// This is the canonical ECS hot-path benchmark.

#include "ecs/components.h"
#include "ecs/world.h"
#include "ecs/system.h"

// Data-layer includes for data_test subcommand.
#include "data/defs/chunk_data.h"
#include "data/defs/world_seed.h"
#include "data/world_gen/terrain_generator.h"
#include "data/world_gen/world_gen_config.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using namespace snt::ecs;
using Clock = std::chrono::high_resolution_clock;

// ---------------------------------------------------------------------------
// MovementSystem — integrates Velocity into Position each tick.
//
// This is the hot path: view<Position, Velocity> iterates all matching
// entities and mutates Position in place. EnTT's SoA layout makes this
// cache-friendly.
// ---------------------------------------------------------------------------
class MovementSystem : public System {
public:
    void update(World& world, float dt) override {
        auto view = world.registry().view<Position, Velocity>();
        for (auto [entity, pos, vel] : view.each()) {
            pos.x += static_cast<int32_t>(vel.vx * dt * 60.0f);
            pos.y += static_cast<int32_t>(vel.vy * dt * 60.0f);
            pos.z += static_cast<int32_t>(vel.vz * dt * 60.0f);
        }
    }
};

// ---------------------------------------------------------------------------
// ecs_test: create N entities with Position+Velocity+Health, run frames.
// ---------------------------------------------------------------------------
static int run_ecs_test(int entity_count, int frame_count) {
    std::cout << "=== ECS Benchmark ===" << std::endl;
    std::cout << "Entities: " << entity_count << std::endl;
    std::cout << "Frames:   " << frame_count << std::endl;
    std::cout << "Components per entity: Position + Velocity + Health" << std::endl;
    std::cout << std::endl;

    World world;

    // --- Phase 1: entity + component creation ---
    auto t0 = Clock::now();
    for (int i = 0; i < entity_count; ++i) {
        auto e = world.create_entity();
        world.registry().emplace<Position>(e, Position{i, 0, 0});
        world.registry().emplace<Velocity>(e, Velocity{1.0f, 0.5f, 0.25f});
        world.registry().emplace<Health>(e, Health{100.0f, 100.0f});
    }
    auto t1 = Clock::now();
    double create_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << "Entity creation: " << create_ms << " ms ("
              << (create_ms / entity_count) << " ms/entity)" << std::endl;

    // --- Phase 2: system registration ---
    world.add_system<MovementSystem>();

    // --- Phase 3: system update loop ---
    const float dt = 1.0f / 60.0f;
    auto t2 = Clock::now();
    for (int f = 0; f < frame_count; ++f) {
        world.update(dt);
    }
    auto t3 = Clock::now();
    double update_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    std::cout << "System update:   " << update_ms << " ms ("
              << (update_ms / frame_count) << " ms/frame)" << std::endl;
    std::cout << std::endl;

    // --- Summary ---
    double total_ms = create_ms + update_ms;
    std::cout << "Total:           " << total_ms << " ms" << std::endl;

    // Sanity check: first entity should have moved.
    auto view = world.registry().view<Position>();
    if (entity_count > 0) {
        auto first = *view.begin();
        auto& pos = world.registry().get<Position>(first);
        std::cout << "Sanity: entity[0] Position = ("
                  << pos.x << ", " << pos.y << ", " << pos.z << ")"
                  << " (expected x ~= " << entity_count + frame_count << ")"
                  << std::endl;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// data_test: generate chunk (0,0,0) via TerrainGenerator and report the
// number of non-air cells. Mirrors the TerrainGenerator smoke test in
// test_data.cpp but as a standalone command-line tool.
// ---------------------------------------------------------------------------
static int run_data_test() {
    using namespace snt::data;

    std::cout << "=== Data Smoke Test ===" << std::endl;
    std::cout << "Generating chunk (0,0,0)..." << std::endl;

    // Build a minimal world-gen config with air + stone roles.
    auto config = std::make_shared<WorldGenConfigSnapshot>();
    TerrainMaterialDef air_def;
    air_def.id = 0;
    air_def.key = "air";
    config->materials.push_back(air_def);
    config->material_ids_by_key["air"] = 0;
    config->material_keys_by_id[0] = "air";
    config->roles.air = 0;
    config->roles.stone = 0;

    TerrainMaterialDef stone_def;
    stone_def.id = 1;
    stone_def.key = "stone";
    config->materials.push_back(stone_def);
    config->material_ids_by_key["stone"] = 1;
    config->material_keys_by_id[1] = "stone";
    config->roles.stone = 1;

    BaseTerrainRule rule;
    rule.dimension_id = "overworld";
    rule.default_material = 1;
    config->base_terrain_rules.push_back(rule);

    config->content_hash = hash_world_gen_config(*config);

    WorldSeed seed(12345);
    TerrainGenerator generator(seed, config);
    ChunkData chunk = generator.generate_chunk("overworld", 0, 0, 0);

    // Count non-air cells (material id != 0).
    int64_t non_air = 0;
    int64_t total = 0;
    for (int i = 0; i < chunk.terrain.size_x; ++i) {
        for (int j = 0; j < chunk.terrain.size_y; ++j) {
            for (int k = 0; k < chunk.terrain.size_z; ++k) {
                ++total;
                if (chunk.terrain.cell_at(i, j, k).material != 0) {
                    ++non_air;
                }
            }
        }
    }

    std::cout << "Chunk dimensions: "
              << chunk.terrain.size_x << "x"
              << chunk.terrain.size_y << "x"
              << chunk.terrain.size_z << std::endl;
    std::cout << "Total cells:      " << total << std::endl;
    std::cout << "Non-air cells:    " << non_air << std::endl;

    return 0;
}

// ---------------------------------------------------------------------------
// Main: dispatch subcommands.
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: engine_test <subcommand> [args...]" << std::endl;
        std::cerr << "Subcommands:" << std::endl;
        std::cerr << "  ecs_test [entities] [frames]  - ECS benchmark (default 1000 entities, 1000 frames)" << std::endl;
        std::cerr << "  data_test                     - TerrainGenerator smoke test (chunk 0,0,0 non-air cells)" << std::endl;
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "ecs_test") {
        int entities = (argc > 2) ? std::atoi(argv[2]) : 1000;
        int frames = (argc > 3) ? std::atoi(argv[3]) : 1000;
        if (entities <= 0) entities = 1000;
        if (frames <= 0) frames = 1000;
        return run_ecs_test(entities, frames);
    }

    if (cmd == "data_test") {
        return run_data_test();
    }

    std::cerr << "Unknown subcommand: " << cmd << std::endl;
    return 1;
}
