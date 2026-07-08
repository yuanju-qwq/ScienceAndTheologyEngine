#version 450

// Mesh vertex shader.
// Inputs: 3D position + 3D color.
// Uniform: MVP matrices (model, view, projection).
// Outputs: clip-space position + color to fragment shader.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inColor;
}
