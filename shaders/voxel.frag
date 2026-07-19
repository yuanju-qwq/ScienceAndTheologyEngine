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

    // Global sun, moon, and ambient inputs are provided by the presentation
    // host. Normals are local, but chunks are axis-aligned so this remains
    // correct until rotated terrain is introduced.
    vec3 n = normalize(fragNormal);
    float sun = max(dot(n, normalize(ubo.sunDirectionIntensity.xyz)), 0.0) *
        max(ubo.sunDirectionIntensity.w, 0.0);
    float moon = max(dot(n, normalize(ubo.moonDirectionIntensity.xyz)), 0.0) *
        max(ubo.moonDirectionIntensity.w, 0.0);
    vec3 light = ubo.ambientColorIntensity.rgb * max(ubo.ambientColorIntensity.w, 0.0) +
        ubo.sunColor.rgb * sun + ubo.moonColor.rgb * moon;

    vec3 color = base * tint * light;
    outColor = vec4(color, 1.0);
}
