// Core ECS components.
//
// This module owns only simulation value types that do not depend on game
// content, presentation, GPU assets, or platform input. Render-facing
// components live in render/render_components.h; ScienceAndTheology gameplay
// components live in game/client/game_components.h.

#pragma once

#include "core/binary_reader.h"
#include "core/binary_writer.h"
#include "core/serializer.h"

#include <cstdint>

namespace snt::ecs {

// Integer simulation position. Voxel gameplay may interpret this as block
// coordinates, while other simulations can use it as a stable grid position.
struct Position {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

// Per-tick simulation velocity. A system owns the integration policy and
// coordinate conversion rather than this neutral value component.
struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
};

}  // namespace snt::ecs

namespace snt::core {

template <>
struct Serializer<snt::ecs::Position> {
    static void write(BinaryWriter& writer, const snt::ecs::Position& position) {
        writer.write_i32(position.x);
        writer.write_i32(position.y);
        writer.write_i32(position.z);
    }

    static bool read(BinaryReader& reader, snt::ecs::Position& position) {
        return reader.read_i32(position.x) &&
               reader.read_i32(position.y) &&
               reader.read_i32(position.z);
    }
};

template <>
struct Serializer<snt::ecs::Velocity> {
    static void write(BinaryWriter& writer, const snt::ecs::Velocity& velocity) {
        writer.write_f32(velocity.vx);
        writer.write_f32(velocity.vy);
        writer.write_f32(velocity.vz);
    }

    static bool read(BinaryReader& reader, snt::ecs::Velocity& velocity) {
        return reader.read_f32(velocity.vx) &&
               reader.read_f32(velocity.vy) &&
               reader.read_f32(velocity.vz);
    }
};

}  // namespace snt::core
