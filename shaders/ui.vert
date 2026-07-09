// UI vertex shader — transforms pixel-space positions to clip space via
// an orthographic projection. Passes UV and color through to the fragment.
//
// Vertex layout (interleaved):
//   location 0: vec2 inPosition  (pixels, top-left origin)
//   location 1: vec2 inUv        (font atlas UV)
//   location 2: vec4 inColor     (RGBA, 0..1)

#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(set = 0, binding = 0) uniform UiUbo {
    mat4 ortho;  // orthographic projection (pixel space -> clip space)
} ubo;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec4 fragColor;

void main() {
    gl_Position = ubo.ortho * vec4(inPosition, 0.0, 1.0);
    fragUv = inUv;
    fragColor = inColor;
}
