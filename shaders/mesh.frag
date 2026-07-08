#version 450

// Mesh fragment shader.
// Inputs: interpolated color from vertex shader.
// Outputs: 4-component color to framebuffer.

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
