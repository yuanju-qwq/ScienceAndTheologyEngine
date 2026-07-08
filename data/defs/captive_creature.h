// CaptiveCreature — persistent individual creature held in a pen.
//
// Ported from src/core/simulation/captive_creature.hpp.
// Namespace: science_and_theology -> snt::data.

#pragma once

#include <cstdint>

#include "defs/creature_species.h"

namespace snt::data {

// ============================================================
// CaptiveCreature — persistent individual creature held in a pen
// ============================================================
//
// A CaptiveCreature is an individual creature that has been detached
// from the wild population dynamics (PopulationCell / Lotka-Volterra)
// by being enclosed in a fence pen and fed by the player.
//
// Lifecycle:
//   1. WILD proxy creature is fed inside a fence-enclosed area.
//   2. capture_creature() removes it from the wild proxy group,
//      reduces wild density, and creates a CaptiveCreature with
//      tame_progress = 0, is_tamed = false, age_stage = ADULT.
//      At this point the creature is "detached" from the wild
//      population — it no longer participates in wild dynamics.
//   3. Each tick, tame_progress accumulates. When it reaches 1.0,
//      is_tamed becomes true (taming complete). Only tamed adults can breed.
//   4. Feeding a tamed adult triggers breeding if a partner
//      (another tamed adult of the same species, not on cooldown)
//      exists in the same pen. After gestation, a BABY is born.
//   5. Babies grow into adults after growth_ticks.
//
// Persistence:
//   CaptiveCreature is stored in ChunkData::captive_creatures
//   (the chunk where the creature was captured). It survives chunk
//   unload/load. Runtime rendering IDs are reassigned on load.
//
// Pen bounds:
//   On capture, a bounded flood-fill determines the enclosed interior.
//   The bounding box of the interior is stored as bounds_min/max and
//   used to clamp wandering so captive creatures stay inside the pen.

enum class CreatureAgeStage : uint8_t {
    BABY  = 0,
    ADULT = 1,
};

constexpr const char* kCreatureAgeStageNames[] = {
    "Baby", "Adult",
};

struct CaptiveCreature {
    // Runtime rendering ID (reassigned on load; not persisted semantically).
    // 0 = unassigned.
    uint64_t runtime_id = 0;

    // Species identifier (references CreatureSpeciesRegistry).
    uint16_t species_id = 0;

    // Cached behavioral role.
    CreatureRole role = CreatureRole::HERBIVORE;

    // Age stage. Babies cannot breed; they grow into adults over time.
    CreatureAgeStage age_stage = CreatureAgeStage::ADULT;

    // Current position (world block coordinates, clamped to pen bounds).
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float pos_z = 0.0f;

    // Wander target position.
    float wander_target_x = 0.0f;
    float wander_target_y = 0.0f;
    float wander_target_z = 0.0f;
    int64_t next_wander_tick = 0;

    // Pen interior bounding box (world block coordinates).
    // Captive creatures are clamped to this box when wandering.
    int32_t bounds_min_x = 0;
    int32_t bounds_min_y = 0;
    int32_t bounds_min_z = 0;
    int32_t bounds_max_x = 0;
    int32_t bounds_max_y = 0;
    int32_t bounds_max_z = 0;

    float health = 1.0f;

    // --- Taming ---

    // Taming progress [0, 1]. Reaches 1.0 = fully tamed.
    float tame_progress = 0.0f;

    // Whether the creature is fully tamed (detached + domesticated).
    bool is_tamed = false;

    // Tick when the creature was captured.
    int64_t capture_tick = 0;

    // --- Growth (for babies) ---

    // Tick when the creature was born (babies) or captured (adults).
    int64_t birth_tick = 0;

    // Tick when a baby becomes an adult. 0 = already adult / N/A.
    int64_t grow_up_tick = 0;

    // --- Breeding ---

    // Tick until which this creature cannot breed again.
    int64_t breed_cooldown_until = 0;

    // Tick when gestation completes and a baby is born. 0 = not pregnant.
    int64_t gestation_end_tick = 0;

    // Whether this creature is currently pregnant (waiting for baby).
    bool is_pregnant = false;

    // Species of the partner that sired the pending baby (for baby species).
    uint16_t partner_species_id = 0;
};

} // namespace snt::data
