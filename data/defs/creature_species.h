// CreatureSpeciesDef — data-driven species definition for creatures.
//
// Ported from src/core/simulation/creature_species.hpp.
// Namespace: science_and_theology -> snt::data.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace snt::data {

// ============================================================
// CreatureRole — behavioral role classification for creatures
// ============================================================
//
// Determines AI behavior pattern:
//   HERBIVORE — wanders, flees from predators
//   PREDATOR  — wanders, does not flee

enum class CreatureRole : uint8_t {
    HERBIVORE = 0,
    PREDATOR  = 1,
    COUNT     = 2,
};

constexpr const char* kCreatureRoleNames[] = {
    "Herbivore", "Predator",
};

// ============================================================
// CreatureDropDef — item dropped when a creature is killed
// ============================================================

struct CreatureDropDef {
    // Item key string (e.g. "snt:rock_lizard_scale").
    std::string item_key;

    // Drop chance [0, 1]. 1.0 = always drops.
    float chance = 1.0f;

    // Minimum count per drop.
    int min_count = 1;

    // Maximum count per drop.
    int max_count = 1;
};

// ============================================================
// CreatureSpeciesDef — data-driven species definition
// ============================================================
//
// Defines a single creature species with its appearance, behavior
// parameters, and loot table. Species are registered in
// CreatureSpeciesRegistry and referenced by species_id.
//
// Design:
//   - CreatureRole (HERBIVORE/PREDATOR) determines AI behavior.
//   - Species-specific parameters (speed, health, flee radius)
//     override the global EcosystemParams defaults.
//   - Drop table links to the source-law system (V0.6).

struct CreatureSpeciesDef {
    // Unique species identifier. 0 = invalid/none.
    uint16_t species_id = 0;

    // Human-readable species key (e.g. "rock_lizard").
    std::string species_key;

    // Translation key (e.g. "creature.rock_lizard").
    std::string title_key;

    // Behavioral role: determines AI state machine.
    CreatureRole role = CreatureRole::HERBIVORE;

    // 3D model resource key for rendering (e.g. "rock_lizard").
    // Godot side resolves this to a scene/resource path.
    std::string model_key;

    // Movement speed in blocks per tick (overrides EcosystemParams).
    // 0.0 = use global default from EcosystemParams.
    float move_speed = 0.0f;

    // Maximum health [0, 1]. 1.0 = full health.
    float base_health = 1.0f;

    // Flee detection radius in blocks (herbivores only).
    // 0.0 = use global default from EcosystemParams.
    float flee_detection_radius = 0.0f;

    // Wander radius in blocks.
    // 0.0 = use global default from EcosystemParams.
    float wander_radius = 0.0f;

    // Scale multiplier for 3D model rendering.
    float model_scale = 1.0f;

    // Biome types where this species naturally spawns.
    // Values match ecosystem_biome constants (kPlains=0, kDesert=1, etc.).
    // Empty = does not spawn via biome pick (may still be spawned by events).
    std::vector<uint8_t> biomes;

    // Items dropped when this creature is killed.
    std::vector<CreatureDropDef> drops;
};

// ============================================================
// CreatureSpeciesRegistry — global registry of species definitions
// ============================================================
//
// Provides O(1) lookup by species_id and by species_key.
// Built-in species are registered from GDScript via GDSpeciesRegistry.
// Additional species can be loaded from JSON (future).

class CreatureSpeciesRegistry {
public:
    CreatureSpeciesRegistry() = default;

    // Register a species definition.
    // If def.species_id == 0, auto-assigns the next sequential ID.
    // Idempotent: if species_key is already registered, returns true and
    // updates def.species_id to the existing ID (no duplicate registration).
    bool register_species(CreatureSpeciesDef& def);

    // Get species definition by ID, or nullptr if not found.
    const CreatureSpeciesDef* get_species(uint16_t species_id) const;

    // Get species definition by key string, or nullptr if not found.
    const CreatureSpeciesDef* get_species_by_key(
        const std::string& key) const;

    // Returns the number of registered species.
    size_t species_count() const { return species_by_id_.size(); }

    // Returns all registered species IDs.
    std::vector<uint16_t> all_species_ids() const;

    // Clear all registered species.
    void clear();

    // Full reset: clears all registration data and resets the species ID
    // counter. Used for tests or hot-reload scenarios.
    static void reset();

    // Import species from another registry (skips duplicates).
    void import_from(const CreatureSpeciesRegistry& other);

    // Global instance for GDScript registration.
    // GDSpeciesRegistry writes here; EcosystemSystem imports from here
    // on initialization if its own registry is empty.
    static CreatureSpeciesRegistry& staging();

private:
    std::unordered_map<uint16_t, CreatureSpeciesDef> species_by_id_;
    std::unordered_map<std::string, uint16_t> id_by_key_;
};

} // namespace snt::data
