#version 450

// Triangle vertex shader.
// Inputs: 2D position + 3-component color.
// Outputs: clip-space position + color to fragment shader.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
}
