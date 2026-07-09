// UI fragment shader — samples the font atlas (single-channel) and
// multiplies by the vertex color. The font atlas is R8: its single
// channel holds the glyph coverage (alpha). We swizzle .r into .a and
// use white for RGB so text color comes from the vertex color.

#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D fontAtlas;

layout(location = 0) out vec4 outColor;

void main() {
    float coverage = fragUv.x < 0.0 ? 1.0 : texture(fontAtlas, fragUv).r;
    outColor = vec4(fragColor.rgb, fragColor.a * coverage);
}
