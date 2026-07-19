// Render ECS components.
//
// These value types describe presentation state. They intentionally sit
// outside snt_ecs so a headless simulation does not inherit mesh-handle or
// camera dependencies. The renderer owns their interpretation and scene
// serialization uses the matching Serializer specializations below.

#pragma once

#include "assets/asset_handle.h"
#include "core/binary_reader.h"
#include "core/binary_writer.h"
#include "core/serializer.h"

#include <array>

namespace snt::render {

struct Transform {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
};

using MeshHandle = snt::assets::MeshHandle;

struct MeshRef {
    MeshHandle handle;
};

struct Camera {
    float fov = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    float aspect = 16.0f / 9.0f;
};

// Global environment-lighting contract shared by game presentation and the
// renderer. Directions point from a shaded surface toward each emitter, so a
// Lambert shader uses dot(surface_normal, direction_to_light).
//
// The client presentation main thread owns writes through
// IRenderLightingController. Render passes receive a value snapshot for the
// frame and never retain this state across frames.
struct DirectionalLight {
    std::array<float, 3> direction_to_light = {0.4f, 0.9f, 0.3f};
    std::array<float, 3> color = {1.0f, 1.0f, 1.0f};
    float intensity = 0.55f;
};

struct EnvironmentLighting {
    DirectionalLight sun{};
    DirectionalLight moon{
        .direction_to_light = {-0.4f, -0.9f, -0.3f},
        .color = {0.6f, 0.65f, 0.8f},
        .intensity = 0.0f,
    };
    std::array<float, 3> ambient_color = {1.0f, 1.0f, 1.0f};
    float ambient_intensity = 0.45f;
    std::array<float, 4> sky_color = {0.0f, 0.0f, 0.2f, 1.0f};
};

// Presentation-only game code uses this narrow boundary instead of reaching
// into RenderSystem or Vulkan resources. Server and simulation targets never
// construct or retain this interface.
class IRenderLightingController {
public:
    virtual ~IRenderLightingController() = default;

    virtual void set_environment_lighting(EnvironmentLighting lighting) noexcept = 0;
    [[nodiscard]] virtual EnvironmentLighting environment_lighting() const noexcept = 0;
};

}  // namespace snt::render

namespace snt::core {

template <>
struct Serializer<snt::render::Transform> {
    static void write(BinaryWriter& writer, const snt::render::Transform& transform) {
        writer.write_raw(transform.position, sizeof(transform.position));
        writer.write_raw(transform.rotation, sizeof(transform.rotation));
        writer.write_raw(transform.scale, sizeof(transform.scale));
    }

    static bool read(BinaryReader& reader, snt::render::Transform& transform) {
        return reader.read_raw(transform.position, sizeof(transform.position)) &&
               reader.read_raw(transform.rotation, sizeof(transform.rotation)) &&
               reader.read_raw(transform.scale, sizeof(transform.scale));
    }
};

template <>
struct Serializer<snt::render::MeshRef> {
    static void write(BinaryWriter& writer, const snt::render::MeshRef& mesh_ref) {
        writer.write_u32(mesh_ref.handle.id);
    }

    static bool read(BinaryReader& reader, snt::render::MeshRef& mesh_ref) {
        return reader.read_u32(mesh_ref.handle.id);
    }
};

template <>
struct Serializer<snt::render::Camera> {
    static void write(BinaryWriter& writer, const snt::render::Camera& camera) {
        writer.write_f32(camera.fov);
        writer.write_f32(camera.near_plane);
        writer.write_f32(camera.far_plane);
        writer.write_f32(camera.aspect);
    }

    static bool read(BinaryReader& reader, snt::render::Camera& camera) {
        return reader.read_f32(camera.fov) &&
               reader.read_f32(camera.near_plane) &&
               reader.read_f32(camera.far_plane) &&
               reader.read_f32(camera.aspect);
    }
};

}  // namespace snt::core
