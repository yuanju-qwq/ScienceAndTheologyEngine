#define SNT_LOG_CHANNEL "player"
#include "core/log.h"

#include "player/player_controller.h"

#include "data/defs/terrain_data.h"
#include "data/world/chunk_registry.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "input/input_system.h"
#include "player/ray_cast.h"
#include "voxel/chunk_render_system.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <cmath>

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

}  // namespace

void PlayerControllerSystem::set_initial_look(float yaw, float pitch) {
    state_.yaw = yaw;
    state_.pitch = std::clamp(pitch, -89.0f, 89.0f);
}

Aabb PlayerControllerSystem::current_body_aabb() const {
    const float half_width = tuning_.width * 0.5f;
    return Aabb{
        .min = {
            state_.feet_position.x - half_width,
            state_.feet_position.y,
            state_.feet_position.z - half_width,
        },
        .max = {
            state_.feet_position.x + half_width,
            state_.feet_position.y + tuning_.height,
            state_.feet_position.z + half_width,
        },
    };
}

Vec3 PlayerControllerSystem::eye_position() const {
    return Vec3{
        state_.feet_position.x,
        state_.feet_position.y + tuning_.eye_height,
        state_.feet_position.z,
    };
}

Vec3 PlayerControllerSystem::look_direction() const {
    const float yaw = degrees_to_radians(state_.yaw);
    const float pitch = degrees_to_radians(state_.pitch);
    const float cp = std::cos(pitch);
    return Vec3{
        std::cos(yaw) * cp,
        std::sin(pitch),
        std::sin(yaw) * cp,
    };
}

void PlayerControllerSystem::sync_camera_transform(snt::ecs::World& world) {
    if (camera_entity_ == entt::null) {
        return;
    }
    auto& registry = world.registry();
    if (!registry.all_of<snt::ecs::Transform>(camera_entity_)) {
        return;
    }

    auto& transform = registry.get<snt::ecs::Transform>(camera_entity_);
    const Vec3 eye = eye_position();
    transform.position[0] = eye.x;
    transform.position[1] = eye.y;
    transform.position[2] = eye.z;
    transform.rotation[0] = state_.pitch;
    transform.rotation[1] = state_.yaw;
    transform.rotation[2] = 0.0f;
}

void PlayerControllerSystem::try_break_target_block() {
    if (!chunk_registry_ || !chunk_render_system_) {
        return;
    }

    CollisionWorldView world_view{
        .chunks = chunk_registry_,
        .dimension_id = dimension_id_,
        .missing_chunks_are_solid = false,
    };
    const Vec3 origin = eye_position();
    const Vec3 forward = look_direction();
    RayCastResult hit =
        ray_cast_voxels_dda(world_view, origin, forward, tuning_.reach_distance);
    if (!hit.hit) {
        const float yaw = degrees_to_radians(state_.yaw);
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
    if (!input_ || !chunk_registry_) {
        sync_camera_transform(world);
        return;
    }

    const auto& input = input_->state();

    if (mouse_locked_) {
        state_.yaw += input.mouse_dx * tuning_.look_speed;
        state_.pitch -= input.mouse_dy * tuning_.look_speed;
        state_.pitch = std::clamp(state_.pitch, -89.0f, 89.0f);

        if (input.mouse_pressed[0]) {
            try_break_target_block();
        }
    }

    const float yaw = degrees_to_radians(state_.yaw);
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
    state_.velocity.x = move_x * speed;
    state_.velocity.z = move_z * speed;

    if (state_.grounded && input.key_pressed[SDL_SCANCODE_SPACE]) {
        state_.velocity.y = tuning_.jump_speed;
        state_.grounded = false;
    }

    state_.velocity.y -= tuning_.gravity * dt;
    if (state_.velocity.y < -tuning_.terminal_velocity) {
        state_.velocity.y = -tuning_.terminal_velocity;
    }

    CollisionWorldView world_view{
        .chunks = chunk_registry_,
        .dimension_id = dimension_id_,
        .missing_chunks_are_solid = false,
    };
    const Vec3 desired_delta{
        state_.velocity.x * dt,
        state_.velocity.y * dt,
        state_.velocity.z * dt,
    };
    const CollisionMoveResult move =
        move_aabb_collide_voxels(world_view, current_body_aabb(), desired_delta);

    state_.feet_position.x += move.delta.x;
    state_.feet_position.y += move.delta.y;
    state_.feet_position.z += move.delta.z;

    if (move.hit_x) {
        state_.velocity.x = 0.0f;
    }
    if (move.hit_z) {
        state_.velocity.z = 0.0f;
    }
    if (move.hit_y) {
        if (state_.velocity.y < 0.0f) {
            state_.grounded = true;
        }
        state_.velocity.y = 0.0f;
    } else {
        state_.grounded = false;
    }

    sync_camera_transform(world);
}

}  // namespace snt::player
