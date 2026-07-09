#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace snt::data {

// Fractal noise generator based on 3D gradient noise (Perlin-style).
// Produces deterministic, seed-based terrain and feature maps.
// All noise is 3D to support spherical planet terrain generation.
//
// Usage:
//   NoiseGenerator noise(seed);
//   float v = noise.noise_3d(x, y, z, 4, 0.5f);
//   float v_scaled = noise.noise_3d_scaled(x, y, z, 0.02f, 4);
class NoiseGenerator {
public:
    static constexpr int kTableSize = 256;

    explicit NoiseGenerator(uint64_t seed);
    ~NoiseGenerator() = default;

    // Fractal (multi-octave) 3D noise. Returns values roughly in [-1, 1].
    // octaves: number of detail layers (higher = more detail, slower).
    // persistence: amplitude multiplier per octave (0.5 = standard).
    float noise_3d(float x, float y, float z, int octaves = 4,
                   float persistence = 0.5f) const;

    // Scaled 3D noise. Convenience wrapper for noise_3d().
    // Smaller scale = larger features.
    float noise_3d_scaled(float x, float y, float z, float scale,
                          int octaves = 4) const;

private:
    void init_permutation(uint64_t seed);

    float raw_noise_3d(float x, float y, float z) const;

    float fade(float t) const;
    float lerp(float a, float b, float t) const;
    float gradient_3d(int hash, float x, float y, float z) const;

    std::array<uint8_t, kTableSize * 2> perm_;
};

// --- Inline implementations ---

inline float NoiseGenerator::fade(float t) const {
    // Perlin's improved fade curve: 6t^5 - 15t^4 + 10t^3
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float NoiseGenerator::lerp(float a, float b, float t) const {
    return a + t * (b - a);
}

inline float NoiseGenerator::gradient_3d(int hash, float x, float y, float z) const {
    // Standard Perlin 3D gradient: low 4 bits select one of 12 edge directions.
    int h = hash & 15;
    float u = (h < 8) ? x : y;
    float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

} // namespace science_and_theology
