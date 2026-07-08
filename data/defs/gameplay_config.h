// GameplayConfig — runtime gameplay configuration (mutable, per-planet overrides).
//
// Ported from src/core/world/gameplay_config.hpp.
// Namespace: science_and_theology -> snt::data.
//
// Design principle: WorldGenConfigSnapshot is frozen and read-only for worker threads.
// GameplayConfig is mutable and only accessed on the main thread or under mutex.

#pragma once

#include <string>
#include <unordered_map>

#include "defs/season_def.h"

namespace snt::data {

// Runtime gameplay configuration, separate from world generation config.
// Controls gameplay systems that operate at runtime (collapse, gravity fall, etc.).
// Can be modified at runtime without regenerating the world.
struct GameplayConfig {
    // --- Collapse system ---

    // Master switch for the cave-in / collapse system.
    // When false, no collapse checks are performed after mining.
    bool enable_collapse = true;

    // Global multiplier applied to each material's collapse_chance.
    // 0.0 = never collapse, 1.0 = normal, 2.0 = very unstable.
    float collapse_chance_multiplier = 1.0f;

    // Maximum number of blocks that can collapse in a single chain reaction.
    // Prevents infinite or excessively large cave-ins.
    int max_collapse_chain = 64;

    // --- Support beam ---

    // Radius (in blocks) within which a support beam prevents collapse.
    // Measured along the gravity direction (not diagonal).
    int support_beam_radius = 5;

    // --- Gravity fall system ---

    // Master switch for gravity-affected blocks (sand, gravel).
    // When false, TF_GRAVITY_FALL blocks behave like normal solid blocks.
    bool enable_gravity_fall = true;

    // Maximum number of blocks that can fall in a single chain reaction.
    // Prevents infinite sand column collapse.
    int max_gravity_fall_chain = 64;

    // --- Day/Night cycle ---

    // Master switch for the day/night cycle.
    // When false, the sun stays at noon permanently.
    bool enable_day_night = true;

    // Day length in real seconds. Default: 600 (10 minutes).
    // At 20 TPS, this equals day_length_seconds * 20 ticks per day.
    // Per-planet overrides can change this for each world.
    float day_length_seconds = 600.0f;

    // Twilight duration as a fraction of the day (0.0-0.5).
    // 0.1 = 10% of the day is sunrise/sunset transition.
    float twilight_fraction = 0.1f;

    // Time of day at world tick 0, in [0, 1). 0.5 is noon.
    float day_start_time = 0.5f;

    // --- Season system ---

    // Number of in-game days per season. Default: 16 days.
    // A full year = 4 x days_per_season days.
    // The actual ticks-per-day is derived from day_length_seconds:
    //   ticks_per_day = day_length_seconds * 20
    int days_per_season = 16;

    // Master switch for seasonal color changes on trees.
    bool enable_season_colors = true;

    // --- Ecosystem system ---

    // Master switch for the ecosystem simulation.
    // When false, no population dynamics are computed.
    bool enable_ecosystem = true;

    // Global multiplier applied to all ecosystem rates.
    // 0.0 = frozen ecosystem, 1.0 = normal, 2.0 = accelerated.
    float ecosystem_rate_multiplier = 1.0f;

    // --- Per-planet overrides ---

    // Per-dimension gameplay config overrides. If a dimension has an entry
    // here, its values take precedence over the global defaults.
    // Missing fields fall back to the global config.
    struct PlanetOverride {
        bool has_enable_collapse = false;
        bool enable_collapse = true;

        bool has_collapse_chance_multiplier = false;
        float collapse_chance_multiplier = 1.0f;

        bool has_max_collapse_chain = false;
        int max_collapse_chain = 64;

        bool has_support_beam_radius = false;
        int support_beam_radius = 5;

        bool has_enable_gravity_fall = false;
        bool enable_gravity_fall = true;

        bool has_max_gravity_fall_chain = false;
        int max_gravity_fall_chain = 64;

        // Day/night per-planet overrides.
        bool has_enable_day_night = false;
        bool enable_day_night = true;

        bool has_day_length_seconds = false;
        float day_length_seconds = 600.0f;

        bool has_twilight_fraction = false;
        float twilight_fraction = 0.1f;

        bool has_day_start_time = false;
        float day_start_time = 0.5f;

        // Ecosystem per-planet overrides.
        bool has_enable_ecosystem = false;
        bool enable_ecosystem = true;

        bool has_ecosystem_rate_multiplier = false;
        float ecosystem_rate_multiplier = 1.0f;
    };

    std::unordered_map<std::string, PlanetOverride> planet_overrides;

    // --- Resolved accessors (apply planet override if present) ---

    bool is_collapse_enabled(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_enable_collapse) {
            return it->second.enable_collapse;
        }
        return enable_collapse;
    }

    float get_collapse_chance_multiplier(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_collapse_chance_multiplier) {
            return it->second.collapse_chance_multiplier;
        }
        return collapse_chance_multiplier;
    }

    int get_max_collapse_chain(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_max_collapse_chain) {
            return it->second.max_collapse_chain;
        }
        return max_collapse_chain;
    }

    int get_support_beam_radius(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_support_beam_radius) {
            return it->second.support_beam_radius;
        }
        return support_beam_radius;
    }

    bool is_gravity_fall_enabled(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_enable_gravity_fall) {
            return it->second.enable_gravity_fall;
        }
        return enable_gravity_fall;
    }

    int get_max_gravity_fall_chain(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_max_gravity_fall_chain) {
            return it->second.max_gravity_fall_chain;
        }
        return max_gravity_fall_chain;
    }

    // --- Day/Night resolved accessors ---

    bool is_day_night_enabled(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_enable_day_night) {
            return it->second.enable_day_night;
        }
        return enable_day_night;
    }

    float get_day_length_seconds(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_day_length_seconds) {
            return it->second.day_length_seconds;
        }
        return day_length_seconds;
    }

    float get_twilight_fraction(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_twilight_fraction) {
            return it->second.twilight_fraction;
        }
        return twilight_fraction;
    }

    float get_day_start_time(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_day_start_time) {
            return it->second.day_start_time;
        }
        return day_start_time;
    }

    // --- Ecosystem resolved accessors ---

    bool is_ecosystem_enabled(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_enable_ecosystem) {
            return it->second.enable_ecosystem;
        }
        return enable_ecosystem;
    }

    float get_ecosystem_rate_multiplier(
        const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() &&
            it->second.has_ecosystem_rate_multiplier) {
            return it->second.ecosystem_rate_multiplier;
        }
        return ecosystem_rate_multiplier;
    }
};

} // namespace snt::data
