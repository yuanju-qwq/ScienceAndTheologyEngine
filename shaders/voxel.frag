#version 450

// Voxel chunk fragment shader.
//
// P3: derives a solid color from material_id via a hash so chunk geometry
//     is visible without textures. A simple directional light (dot with an
//     up-ish sun vector) plus face-type tinting (top brighter, sides
//     neutral, bottom darker) gives shape readability.
//
// P4: replace the hash with an atlas texture lookup using face_type to
//     pick the per-face tile (top/bottom/side variants).

layout(location = 0) in vec3 fragNormal;
layout(location = 1) flat in uint fragMaterialId;
layout(location = 2) in float fragFaceType;
layout(location = 3) in vec2 fragUv;

layout(set = 0, binding = 1) uniform sampler2D materialAtlas;

layout(location = 0) out vec4 outColor;

vec3 material_color(uint id, vec2 uv) {
    const float tile_count = 4.0;
    float tile = clamp(float(id == 0u ? 0u : id - 1u), 0.0, tile_count - 1.0);
    vec2 tile_uv = fract(uv);
    vec2 atlas_uv = vec2((tile + tile_uv.x) / tile_count, tile_uv.y);
    vec4 sampled = texture(materialAtlas, atlas_uv);
    return sampled.rgb;
}

void main() {
    vec3 base = material_color(fragMaterialId, fragUv);

    // Face-type tint: 0=top (brightest), 1=bottom (darkest), 2=sides.
    float tint = 1.0;
    if (fragFaceType < 0.5) {
        tint = 1.15;          // top
    } else if (fragFaceType < 1.5) {
        tint = 0.55;          // bottom
    } else {
        tint = 0.85;          // sides
    }

    // Simple directional lighting from above-front. Normal is local, but
    // chunks are axis-aligned so this still gives readable shading.
    vec3 n = normalize(fragNormal);
    vec3 light_dir = normalize(vec3(0.4, 0.9, 0.3));
    float ndotl = max(dot(n, light_dir), 0.0);
    float ambient = 0.45;
    float light = ambient + (1.0 - ambient) * ndotl;

    vec3 color = base * tint * light;
    outColor = vec4(color, 1.0);
}
