// CreatureSpeciesRegistry implementation.
//
// Ported from src/core/simulation/creature_species.cpp.
// Namespace: science_and_theology -> snt::data.

#include "defs/creature_species.h"

namespace snt::data {

// --- CreatureSpeciesRegistry ---

bool CreatureSpeciesRegistry::register_species(
    CreatureSpeciesDef& def) {
    if (def.species_key.empty()) return false;
    // Idempotent: if species_key is already registered, return true and
    // update def.species_id to the existing ID via the reference parameter.
    auto it = id_by_key_.find(def.species_key);
    if (it != id_by_key_.end()) {
        def.species_id = it->second;
        return true;
    }
    // Force explicit ID: species_id == 0 is rejected (auto-allocation not supported).
    if (def.species_id == 0) {
        return false;
    }
    if (species_by_id_.find(def.species_id) != species_by_id_.end()) {
        return false;
    }
    species_by_id_[def.species_id] = def;
    id_by_key_[def.species_key] = def.species_id;
    return true;
}

const CreatureSpeciesDef* CreatureSpeciesRegistry::get_species(
    uint16_t species_id) const {
    auto it = species_by_id_.find(species_id);
    return (it != species_by_id_.end()) ? &it->second : nullptr;
}

const CreatureSpeciesDef* CreatureSpeciesRegistry::get_species_by_key(
    const std::string& key) const {
    auto kit = id_by_key_.find(key);
    if (kit == id_by_key_.end()) return nullptr;
    return get_species(kit->second);
}

std::vector<uint16_t> CreatureSpeciesRegistry::all_species_ids() const {
    std::vector<uint16_t> ids;
    ids.reserve(species_by_id_.size());
    for (const auto& [id, _] : species_by_id_) {
        ids.push_back(id);
    }
    return ids;
}

void CreatureSpeciesRegistry::clear() {
    species_by_id_.clear();
    id_by_key_.clear();
}

void CreatureSpeciesRegistry::reset() {
    // Full reset: clears staging registration data.
    // species_id is now explicitly allocated by the GD side, no internal
    // ID counter to reset.
    staging().clear();
}

void CreatureSpeciesRegistry::import_from(
    const CreatureSpeciesRegistry& other) {
    for (const auto& [id, def] : other.species_by_id_) {
        if (species_by_id_.find(id) == species_by_id_.end()) {
            species_by_id_[id] = def;
            id_by_key_[def.species_key] = id;
        }
    }
}

CreatureSpeciesRegistry& CreatureSpeciesRegistry::staging() {
    static CreatureSpeciesRegistry g_staging;
    return g_staging;
}

} // namespace snt::data
