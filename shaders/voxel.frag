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

layout(location = 0) out vec4 outColor;

// Hash material_id -> stable RGB in [0,1]. Cheap integer hash so each
// material gets a visually distinct color without a lookup table.
vec3 material_color(uint id) {
    uint h = id * 2654435761u;
    float r = float((h >>  0) & 0xFF) / 255.0;
    float g = float((h >>  8) & 0xFF) / 255.0;
    float b = float((h >> 16) & 0xFF) / 255.0;
    // Bias toward mid-bright so geometry reads well on a dark clear color.
    return 0.3 + 0.7 * vec3(r, g, b);
}

void main() {
    vec3 base = material_color(fragMaterialId);

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
