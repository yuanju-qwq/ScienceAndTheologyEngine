// PlayerControllerSystem main-thread input and interaction implementation.

#define SNT_LOG_CHANNEL "player"
#include "core/log.h"

#include "player/player_controller.h"
#include "player/player_physics_system.h"

#include "data/defs/terrain_data.h"
#include "data/world/chunk_registry.h"
#include "ecs/world.h"
#include "input/input_system.h"
#include "player/ray_cast.h"
#include "voxel/chunk_render_system.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace snt::player {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int32_t kChunkSize = snt::data::ChunkData::kChunkSize;

float degrees_to_radians(float deg) {
    return deg * kPi / 180.0f;
}

void normalize_2d(float& x, float& z) {
    const float len_sq = x * x + z * z;
    if (len_sq <= 0.000001f) {
        x = 0.0f;
        z = 0.0f;
        return;
    }
    const float inv_len = 1.0f / std::sqrt(len_sq);
    x *= inv_len;
    z *= inv_len;
}

Vec3 normalize_3d(Vec3 v) {
    const float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len_sq <= 0.000001f) {
        return {};
    }
    const float inv_len = 1.0f / std::sqrt(len_sq);
    v.x *= inv_len;
    v.y *= inv_len;
    v.z *= inv_len;
    return v;
}

Vec3 add_scaled(Vec3 base, Vec3 offset, float scale) {
    base.x += offset.x * scale;
    base.y += offset.y * scale;
    base.z += offset.z * scale;
    return base;
}

Vec3 eye_position(const PlayerControllerState& state,
                  const PlayerControllerTuning& tuning) {
    return {
        state.feet_position.x,
        state.feet_position.y + tuning.eye_height,
        state.feet_position.z,
    };
}

Vec3 look_direction(const PlayerControllerState& state) {
    const float yaw = degrees_to_radians(state.yaw);
    const float pitch = degrees_to_radians(state.pitch);
    const float cos_pitch = std::cos(pitch);
    return {
        std::cos(yaw) * cos_pitch,
        std::sin(pitch),
        std::sin(yaw) * cos_pitch,
    };
}

}  // namespace

void PlayerControllerSystem::set_initial_look(float yaw, float pitch) {
    initial_state_.yaw = yaw;
    initial_state_.pitch = std::clamp(pitch, -89.0f, 89.0f);
}

std::shared_ptr<PlayerPhysicsSystem> PlayerControllerSystem::make_physics_system() const {
    return std::make_shared<PlayerPhysicsSystem>(
        chunk_registry_, camera_entity_, dimension_id_, tuning_);
}

PlayerControllerState* PlayerControllerSystem::ensure_state(snt::ecs::World& world) {
    if (camera_entity_ == entt::null) {
        return nullptr;
    }

    auto& registry = world.registry();
    if (!registry.valid(camera_entity_)) {
        return nullptr;
    }
    if (!registry.all_of<PlayerControllerState>(camera_entity_)) {
        registry.emplace<PlayerControllerState>(camera_entity_, initial_state_);
    }
    return &registry.get<PlayerControllerState>(camera_entity_);
}

void PlayerControllerSystem::try_break_target_block(const PlayerControllerState& state) {
    if (!chunk_registry_ || !chunk_render_system_) {
        return;
    }

    const CollisionWorldView world_view(chunk_registry_, dimension_id_, false);
    const Vec3 origin = eye_position(state, tuning_);
    const Vec3 forward = look_direction(state);
    RayCastResult hit =
        ray_cast_voxels_dda(world_view, origin, forward, tuning_.reach_distance);
    if (!hit.hit) {
        const float yaw = degrees_to_radians(state.yaw);
        const Vec3 right{-std::sin(yaw), 0.0f, std::cos(yaw)};
        const Vec3 up = normalize_3d({
            right.y * forward.z - right.z * forward.y,
            right.z * forward.x - right.x * forward.z,
            right.x * forward.y - right.y * forward.x,
        });
        constexpr float kPickAperture = 0.015f;
        const std::array<Vec3, 8> offsets = {
            right,
            {-right.x, -right.y, -right.z},
            up,
            {-up.x, -up.y, -up.z},
            normalize_3d({right.x + up.x, right.y + up.y, right.z + up.z}),
            normalize_3d({right.x - up.x, right.y - up.y, right.z - up.z}),
            normalize_3d({-right.x + up.x, -right.y + up.y, -right.z + up.z}),
            normalize_3d({-right.x - up.x, -right.y - up.y, -right.z - up.z}),
        };

        for (const Vec3& offset : offsets) {
            RayCastResult candidate = ray_cast_voxels_dda(
                world_view,
                origin,
                add_scaled(forward, offset, kPickAperture),
                tuning_.reach_distance);
            if (candidate.hit && (!hit.hit || candidate.distance < hit.distance)) {
                hit = candidate;
            }
        }
        if (hit.hit) {
            SNT_LOG_DEBUG("Block pick aperture hit world=(%d,%d,%d), distance=%.3f",
                          hit.block.x, hit.block.y, hit.block.z, hit.distance);
        }
    }
    if (!hit.hit) {
        return;
    }

    const int32_t cx = floor_div_i32(hit.block.x, kChunkSize);
    const int32_t cy = floor_div_i32(hit.block.y, kChunkSize);
    const int32_t cz = floor_div_i32(hit.block.z, kChunkSize);
    const int32_t lx = positive_mod_i32(hit.block.x, kChunkSize);
    const int32_t ly = positive_mod_i32(hit.block.y, kChunkSize);
    const int32_t lz = positive_mod_i32(hit.block.z, kChunkSize);

    snt::data::ChunkData* chunk = chunk_registry_->get_chunk(dimension_id_, cx, cy, cz);
    if (!chunk || !chunk->terrain.is_valid_cell(lx, ly, lz)) {
        SNT_LOG_WARN("Break block target missing chunk/cell at world=(%d,%d,%d)",
                     hit.block.x, hit.block.y, hit.block.z);
        return;
    }

    auto& cell = chunk->terrain.cell_at(lx, ly, lz);
    if (!cell.is_solid() || cell.is_indestructible()) {
        return;
    }

    cell.material = 0;
    cell.flags = 0;
    cell.clear_fluid();

    const snt::data::ChunkKey dirty_key(dimension_id_, cx, cy, cz);
    chunk_render_system_->mark_dirty(dirty_key);
    if (lx == 0) chunk_render_system_->mark_dirty({dimension_id_, cx - 1, cy, cz});
    if (lx == kChunkSize - 1) chunk_render_system_->mark_dirty({dimension_id_, cx + 1, cy, cz});
    if (ly == 0) chunk_render_system_->mark_dirty({dimension_id_, cx, cy - 1, cz});
    if (ly == kChunkSize - 1) chunk_render_system_->mark_dirty({dimension_id_, cx, cy + 1, cz});
    if (lz == 0) chunk_render_system_->mark_dirty({dimension_id_, cx, cy, cz - 1});
    if (lz == kChunkSize - 1) chunk_render_system_->mark_dirty({dimension_id_, cx, cy, cz + 1});

    SNT_LOG_INFO("Broke block at world=(%d,%d,%d), chunk=(%d,%d,%d), local=(%d,%d,%d)",
                 hit.block.x, hit.block.y, hit.block.z,
                 cx, cy, cz, lx, ly, lz);
}

void PlayerControllerSystem::update(snt::ecs::World& world, float dt) {
    PlayerControllerState* player_state = ensure_state(world);
    if (!player_state || !input_ || !chunk_registry_) {
        return;
    }

    const auto& input = input_->state();

    if (mouse_locked_) {
        player_state->yaw += input.mouse_dx * tuning_.look_speed;
        player_state->pitch -= input.mouse_dy * tuning_.look_speed;
        player_state->pitch = std::clamp(player_state->pitch, -89.0f, 89.0f);

        if (input.mouse_pressed[0]) {
            try_break_target_block(*player_state);
        }
    }

    const float yaw = degrees_to_radians(player_state->yaw);
    const Vec3 forward{std::cos(yaw), 0.0f, std::sin(yaw)};
    const Vec3 right{-std::sin(yaw), 0.0f, std::cos(yaw)};

    float move_x = 0.0f;
    float move_z = 0.0f;
    if (input.key_held[SDL_SCANCODE_W]) {
        move_x += forward.x;
        move_z += forward.z;
    }
    if (input.key_held[SDL_SCANCODE_S]) {
        move_x -= forward.x;
        move_z -= forward.z;
    }
    if (input.key_held[SDL_SCANCODE_D]) {
        move_x += right.x;
        move_z += right.z;
    }
    if (input.key_held[SDL_SCANCODE_A]) {
        move_x -= right.x;
        move_z -= right.z;
    }
    normalize_2d(move_x, move_z);

    float speed = tuning_.move_speed;
    if (input.key_held[SDL_SCANCODE_LSHIFT]) {
        speed *= tuning_.sprint_multiplier;
    }
    player_state->velocity.x = move_x * speed;
    player_state->velocity.z = move_z * speed;

    if (player_state->grounded && input.key_pressed[SDL_SCANCODE_SPACE]) {
        player_state->velocity.y = tuning_.jump_speed;
        player_state->grounded = false;
    }

    player_state->velocity.y -= tuning_.gravity * dt;
    if (player_state->velocity.y < -tuning_.terminal_velocity) {
        player_state->velocity.y = -tuning_.terminal_velocity;
    }
}

}  // namespace snt::player
