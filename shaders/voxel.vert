#version 450

// Voxel chunk vertex shader.
// Inputs: position + normal + material_id + face_type.
// Uniform: MVP matrices (model per chunk, view/proj per frame).
// Outputs: clip-space position + material_id + face_type + world normal
//          to fragment shader.
//
// P3: material_id + face_type are forwarded so the fragment shader can
//     derive a solid color per material. P4 will add a texture atlas
//     binding and use these to look up UVs per-face.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in uint inMaterialId;
layout(location = 3) in float inFaceType;
layout(location = 4) in vec2 inUv;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) flat out uint fragMaterialId;
layout(location = 2) out float fragFaceType;
layout(location = 3) out vec2 fragUv;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    // Normal is in chunk-local space; lighting uses it as-is. P4 will add
    // a normal matrix when chunks rotate, but chunks are axis-aligned so
    // the model matrix is a pure translation.
    fragNormal = inNormal;
    fragMaterialId = inMaterialId;
    fragFaceType = inFaceType;
    fragUv = inUv;
}
