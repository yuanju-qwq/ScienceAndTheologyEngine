// UI vertex shader — transforms pixel-space positions to clip space via
// an orthographic projection. Texture mode distinguishes solid MUI shapes,
// signed-distance glyphs and pre-coloured emoji glyphs.

#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uint inTextureMode;

layout(set = 0, binding = 0) uniform UiUbo {
    mat4 ortho;
} ubo;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec4 fragColor;
layout(location = 2) flat out uint fragTextureMode;

void main() {
    gl_Position = ubo.ortho * vec4(inPosition, 0.0, 1.0);
    fragUv = inUv;
    fragColor = inColor;
    fragTextureMode = inTextureMode;
}