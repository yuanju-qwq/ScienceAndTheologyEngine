// UI fragment shader — renders MUI solid primitives, signed-distance text,
// and RGBA colour glyphs from the dynamic Unicode atlas.

#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;
layout(location = 2) flat in uint fragTextureMode;

layout(set = 0, binding = 1) uniform sampler2D glyphAtlas;

layout(location = 0) out vec4 outColor;

void main() {
    if (fragTextureMode == 0u) {
        outColor = fragColor;
        return;
    }

    vec4 sampleColor = texture(glyphAtlas, fragUv);
    if (fragTextureMode == 1u) {
        float smoothing = max(fwidth(sampleColor.a), 0.001);
        float coverage = smoothstep(0.5 - smoothing, 0.5 + smoothing, sampleColor.a);
        outColor = vec4(fragColor.rgb, fragColor.a * coverage);
        return;
    }

    outColor = sampleColor * fragColor;
}