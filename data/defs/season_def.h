// Season — enum and utilities for the seasonal cycle.
//
// Ported from src/core/simulation/season_def.hpp.
// Namespace: science_and_theology -> snt::data.

#pragma once

#include <cstdint>
#include <string>

namespace snt::data {

// ============================================================
// Season — enum and utilities for the seasonal cycle
// ============================================================

enum class Season : uint8_t {
    SPRING = 0,
    SUMMER = 1,
    AUTUMN = 2,
    WINTER = 3,
    COUNT  = 4,
};

constexpr const char* kSeasonNames[] = {
    "Spring", "Summer", "Autumn", "Winter",
};

// Season-specific color tint modifiers for leaves.
// Applied as multipliers to the base leaves color.
struct SeasonColorMod {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

// Default color modifiers per season for deciduous trees.
// Spring: fresh light green
// Summer: deep green (no change)
// Autumn: warm yellow/orange
// Winter: bare (very dark, almost invisible leaves)
constexpr SeasonColorMod kDeciduousSeasonMods[] = {
    {1.0f,  1.1f,  0.9f},   // Spring: slightly brighter green
    {1.0f,  1.0f,  1.0f},   // Summer: base color
    {1.1f,  0.85f, 0.5f},   // Autumn: warm shift
    {0.4f,  0.35f, 0.3f},   // Winter: dark/bare
};

// Default color modifiers per season for evergreen trees.
// Minimal change throughout the year.
constexpr SeasonColorMod kEvergreenSeasonMods[] = {
    {1.0f,  1.0f,  1.0f},   // Spring: base
    {1.0f,  1.0f,  1.0f},   // Summer: base
    {0.95f, 0.95f, 0.9f},   // Autumn: very slight warm
    {0.85f, 0.9f,  0.85f},  // Winter: slightly darker
};

// Compute the current season from a game tick counter, days_per_season,
// and ticks_per_day (derived from day_length_seconds * TPS).
inline Season season_from_tick(int64_t tick, int days_per_season,
                               int64_t ticks_per_day = 12000) {
    if (days_per_season <= 0) days_per_season = 16;
    if (ticks_per_day <= 0) ticks_per_day = 12000;
    const int64_t ticks_per_season =
        static_cast<int64_t>(days_per_season) * ticks_per_day;
    const int64_t ticks_per_year = ticks_per_season * 4;
    if (ticks_per_year <= 0) return Season::SPRING;
    const int64_t tick_in_year = tick % ticks_per_year;
    const int season_index = static_cast<int>(tick_in_year / ticks_per_season);
    if (season_index < 0 || season_index >= 4) return Season::SPRING;
    return static_cast<Season>(season_index);
}

// Compute the day within the current season (0-based).
inline int day_in_season(int64_t tick, int days_per_season,
                         int64_t ticks_per_day = 12000) {
    if (days_per_season <= 0) days_per_season = 16;
    if (ticks_per_day <= 0) ticks_per_day = 12000;
    const int64_t ticks_per_season =
        static_cast<int64_t>(days_per_season) * ticks_per_day;
    if (ticks_per_season <= 0) return 0;
    const int64_t tick_in_season = tick % ticks_per_season;
    return static_cast<int>(tick_in_season / ticks_per_day);
}

// Compute the total game day (0-based, counting from tick 0).
inline int64_t total_game_day(int64_t tick, int64_t ticks_per_day = 12000) {
    if (ticks_per_day <= 0) ticks_per_day = 12000;
    return tick / ticks_per_day;
}

} // namespace snt::data
