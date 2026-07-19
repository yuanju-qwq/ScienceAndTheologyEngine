#version 450

// Mesh fragment shader.
// Inputs: interpolated color from vertex shader.
// Outputs: 4-component color to framebuffer.

layout(location = 0) in vec3 fragColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 sunDirectionIntensity;
    vec4 sunColor;
    vec4 moonDirectionIntensity;
    vec4 moonColor;
    vec4 ambientColorIntensity;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    // Generic meshes currently carry no normal attribute. They still receive
    // the environment contribution; a later normal-bearing material path can
    // consume the directional terms without changing this global interface.
    vec3 environment = ubo.ambientColorIntensity.rgb *
        max(ubo.ambientColorIntensity.w, 0.0) +
        ubo.sunColor.rgb * max(ubo.sunDirectionIntensity.w, 0.0) +
        ubo.moonColor.rgb * max(ubo.moonDirectionIntensity.w, 0.0);
    outColor = vec4(fragColor * environment, 1.0);
}
