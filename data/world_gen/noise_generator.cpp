#include "noise_generator.h"

#include <algorithm>
#include <random>

namespace snt::data {

NoiseGenerator::NoiseGenerator(uint64_t seed) {
    init_permutation(seed);
}

void NoiseGenerator::init_permutation(uint64_t seed) {
    // Fill identity permutation.
    for (int i = 0; i < kTableSize; ++i) {
        perm_[i] = static_cast<uint8_t>(i);
    }

    // Shuffle using Fisher-Yates with a deterministic RNG.
    std::mt19937_64 rng(seed);
    for (int i = kTableSize - 1; i > 0; --i) {
        int j = static_cast<int>(rng() % (static_cast<uint64_t>(i) + 1));
        std::swap(perm_[i], perm_[j]);
    }

    // Duplicate for wrapping.
    for (int i = 0; i < kTableSize; ++i) {
        perm_[kTableSize + i] = perm_[i];
    }
}

float NoiseGenerator::noise_3d(
    float x, float y, float z, int octaves, float persistence) const {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float max_value = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        total += raw_noise_3d(x * frequency, y * frequency, z * frequency) * amplitude;
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }

    return total / max_value;
}

float NoiseGenerator::noise_3d_scaled(
    float x, float y, float z, float scale, int octaves) const {
    return noise_3d(x * scale, y * scale, z * scale, octaves);
}

float NoiseGenerator::raw_noise_3d(float x, float y, float z) const {
    // Integer part.
    int xi = static_cast<int>(std::floor(x)) & (kTableSize - 1);
    int yi = static_cast<int>(std::floor(y)) & (kTableSize - 1);
    int zi = static_cast<int>(std::floor(z)) & (kTableSize - 1);

    // Fractional part.
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float zf = z - std::floor(z);

    // Fade curves.
    float u = fade(xf);
    float v = fade(yf);
    float w = fade(zf);

    // Hash corners of the unit cube.
    int aaa = perm_[perm_[perm_[xi] + yi] + zi];
    int baa = perm_[perm_[perm_[xi + 1] + yi] + zi];
    int aba = perm_[perm_[perm_[xi] + yi + 1] + zi];
    int bba = perm_[perm_[perm_[xi + 1] + yi + 1] + zi];
    int aab = perm_[perm_[perm_[xi] + yi] + zi + 1];
    int bab = perm_[perm_[perm_[xi + 1] + yi] + zi + 1];
    int abb = perm_[perm_[perm_[xi] + yi + 1] + zi + 1];
    int bbb = perm_[perm_[perm_[xi + 1] + yi + 1] + zi + 1];

    // Interpolate along X.
    float x1 = lerp(gradient_3d(aaa, xf, yf, zf),
                    gradient_3d(baa, xf - 1.0f, yf, zf), u);
    float x2 = lerp(gradient_3d(aba, xf, yf - 1.0f, zf),
                    gradient_3d(bba, xf - 1.0f, yf - 1.0f, zf), u);
    float x3 = lerp(gradient_3d(aab, xf, yf, zf - 1.0f),
                    gradient_3d(bab, xf - 1.0f, yf, zf - 1.0f), u);
    float x4 = lerp(gradient_3d(abb, xf, yf - 1.0f, zf - 1.0f),
                    gradient_3d(bbb, xf - 1.0f, yf - 1.0f, zf - 1.0f), u);

    // Interpolate along Y.
    float y1 = lerp(x1, x2, v);
    float y2 = lerp(x3, x4, v);

    // Interpolate along Z. Scale from [-0.5, 0.5] to roughly [-1, 1].
    return lerp(y1, y2, w) * 2.0f;
}

} // namespace science_and_theology
