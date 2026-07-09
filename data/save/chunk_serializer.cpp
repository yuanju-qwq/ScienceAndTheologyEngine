#include "chunk_serializer.h"

#include <cstring>

#include "../defs/creature_species.h"

namespace snt::data {

// --- Serialize ---

std::vector<uint8_t> ChunkSerializer::serialize(
    const std::string& dimension_id, const ChunkData& chunk) {
    std::vector<uint8_t> buf;

    // Header.
    write_uint8(buf, kCurrentVersion);
    write_int32(buf, chunk.chunk_x);
    write_int32(buf, chunk.chunk_y);
    write_int32(buf, chunk.chunk_z);
    write_uint8(buf, static_cast<uint8_t>(chunk.state));
    write_string(buf, dimension_id);

    // Terrain.
    int size_x = chunk.terrain.size_x;
    int size_y = chunk.terrain.size_y;
    int size_z = chunk.terrain.size_z;
    int cell_count = size_x * size_y * size_z;
    write_uint32(buf, static_cast<uint32_t>(size_x));
    write_uint32(buf, static_cast<uint32_t>(size_y));
    write_uint32(buf, static_cast<uint32_t>(size_z));
    write_uint32(buf, static_cast<uint32_t>(cell_count));

    // Materials (uint8 per cell).
    for (int i = 0; i < cell_count; ++i) {
        write_uint8(buf, static_cast<uint8_t>(chunk.terrain.cells[i].material));
    }

    // Flags (uint32 per cell).
    for (int i = 0; i < cell_count; ++i) {
        write_uint32(buf, chunk.terrain.cells[i].flags);
    }

    // Connectors.
    write_uint32(buf, static_cast<uint32_t>(chunk.connectors.size()));
    for (const auto& conn : chunk.connectors) {
        write_connector(buf, conn);
    }

    // Mechanisms.
    write_uint32(buf, static_cast<uint32_t>(chunk.mechanisms.size()));
    for (const auto& mechanism : chunk.mechanisms) {
        write_mechanism(buf, mechanism);
    }

    // Entity IDs.
    write_uint32(buf, static_cast<uint32_t>(chunk.entities.size()));
    for (const auto& eid : chunk.entities) {
        write_uint64(buf, eid.id);
    }

    // Machine IDs.
    write_uint32(buf, static_cast<uint32_t>(chunk.machines.size()));
    for (const auto& mid : chunk.machines) {
        write_uint64(buf, mid.id);
    }

    // Connector IDs.
    write_uint32(buf,
                 static_cast<uint32_t>(chunk.connector_ids.size()));
    for (const auto& cid : chunk.connector_ids) {
        write_uint64(buf, cid.id);
    }

    // Block entities.
    write_uint32(buf, static_cast<uint32_t>(chunk.block_entities.size()));
    for (const auto& be : chunk.block_entities) {
        write_block_entity(buf, be);
    }

    // Population cell (ecosystem data, version 6+).
    write_uint8(buf, chunk.has_population_cell ? 1 : 0);
    if (chunk.has_population_cell) {
        write_population_cell(buf, chunk.population_cell);
    }

    // Captive creatures (husbandry data, version 8+).
    write_uint8(buf, chunk.has_captive_creatures ? 1 : 0);
    if (chunk.has_captive_creatures) {
        write_uint32(buf, static_cast<uint32_t>(chunk.captive_creatures.size()));
        for (const auto& cc : chunk.captive_creatures) {
            write_captive_creature(buf, cc);
        }
    }

    return buf;
}

// --- Deserialize ---

bool ChunkSerializer::deserialize(
    const std::vector<uint8_t>& data,
    std::string& dimension_id, ChunkData& chunk) {
    size_t offset = 0;

    uint8_t version;
    if (!read_uint8(data, offset, version)) return false;
    // Support version 4 (pre-block-entity) and version 5 (with block entities).
    if (version < 4 || version > kCurrentVersion) return false;

    int32_t cx, cy, cz;
    if (!read_int32(data, offset, cx)) return false;
    if (!read_int32(data, offset, cy)) return false;
    if (!read_int32(data, offset, cz)) return false;
    chunk.chunk_x = cx;
    chunk.chunk_y = cy;
    chunk.chunk_z = cz;

    uint8_t state_byte;
    if (!read_uint8(data, offset, state_byte)) return false;
    chunk.state = static_cast<ChunkState>(state_byte);

    if (!read_string(data, offset, dimension_id)) return false;

    // Terrain.
    uint32_t sx, sy, sz, cc;
    if (!read_uint32(data, offset, sx)) return false;
    if (!read_uint32(data, offset, sy)) return false;
    if (!read_uint32(data, offset, sz)) return false;
    if (!read_uint32(data, offset, cc)) return false;

    int size_x = static_cast<int>(sx);
    int size_y = static_cast<int>(sy);
    int size_z = static_cast<int>(sz);
    int cell_count = static_cast<int>(cc);

    if (cell_count != size_x * size_y * size_z) return false;
    if (cell_count <= 0 ||
        cell_count > ChunkData::kChunkSize * ChunkData::kChunkSize * ChunkData::kChunkSize) {
        return false;
    }

    chunk.terrain.resize(size_x, size_y, size_z);

    // Materials.
    for (int i = 0; i < cell_count; ++i) {
        uint8_t mat;
        if (!read_uint8(data, offset, mat)) return false;
        chunk.terrain.cells[i].material = static_cast<TerrainMaterial>(mat);
    }

    // Flags.
    for (int i = 0; i < cell_count; ++i) {
        uint32_t flags;
        if (!read_uint32(data, offset, flags)) return false;
        chunk.terrain.cells[i].flags = flags;
    }

    // Connectors.
    uint32_t conn_count;
    if (!read_uint32(data, offset, conn_count)) return false;
    chunk.connectors.clear();
    chunk.connectors.reserve(conn_count);
    for (uint32_t i = 0; i < conn_count; ++i) {
        ConnectorPlacement conn;
        if (!read_connector(data, offset, conn)) return false;
        chunk.connectors.push_back(std::move(conn));
    }

    // Mechanisms.
    chunk.mechanisms.clear();
    uint32_t mechanism_count;
    if (!read_uint32(data, offset, mechanism_count)) return false;
    chunk.mechanisms.reserve(mechanism_count);
    for (uint32_t i = 0; i < mechanism_count; ++i) {
        MechanismPlacement mechanism;
        if (!read_mechanism(data, offset, mechanism)) return false;
        chunk.mechanisms.push_back(std::move(mechanism));
    }

    // Entity IDs.
    uint32_t entity_count;
    if (!read_uint32(data, offset, entity_count)) return false;
    chunk.entities.clear();
    chunk.entities.reserve(entity_count);
    for (uint32_t i = 0; i < entity_count; ++i) {
        uint64_t id;
        if (!read_uint64(data, offset, id)) return false;
        chunk.entities.push_back(EntityId{id});
    }

    // Machine IDs.
    uint32_t machine_count;
    if (!read_uint32(data, offset, machine_count)) return false;
    chunk.machines.clear();
    chunk.machines.reserve(machine_count);
    for (uint32_t i = 0; i < machine_count; ++i) {
        uint64_t id;
        if (!read_uint64(data, offset, id)) return false;
        chunk.machines.push_back(MachineId{id});
    }

    // Connector IDs.
    uint32_t conn_id_count;
    if (!read_uint32(data, offset, conn_id_count)) return false;
    chunk.connector_ids.clear();
    chunk.connector_ids.reserve(conn_id_count);
    for (uint32_t i = 0; i < conn_id_count; ++i) {
        uint64_t id;
        if (!read_uint64(data, offset, id)) return false;
        chunk.connector_ids.push_back(ConnectorId{id});
    }

    // Block entities (version 5+).
    if (version >= 5) {
        uint32_t be_count;
        if (!read_uint32(data, offset, be_count)) return false;
        chunk.block_entities.clear();
        chunk.block_entities.reserve(be_count);
        for (uint32_t i = 0; i < be_count; ++i) {
            BlockEntityPlacement be;
            if (!read_block_entity(data, offset, be)) return false;
            chunk.block_entities.push_back(std::move(be));
        }
    }

    // Population cell (ecosystem data, version 6+).
    if (version >= 6) {
        uint8_t has_pop;
        if (!read_uint8(data, offset, has_pop)) return false;
        chunk.has_population_cell = (has_pop != 0);
        if (chunk.has_population_cell) {
            if (!read_population_cell(data, offset, chunk.population_cell)) {
                return false;
            }
        }
    } else {
        chunk.has_population_cell = false;
    }

    // Captive creatures (v8+).
    if (version >= 8) {
        uint8_t has_captive;
        if (!read_uint8(data, offset, has_captive)) return false;
        chunk.has_captive_creatures = (has_captive != 0);
        if (chunk.has_captive_creatures) {
            uint32_t captive_count;
            if (!read_uint32(data, offset, captive_count)) return false;
            // Sanity cap to avoid corrupt data causing huge allocations.
            if (captive_count > 4096) return false;
            chunk.captive_creatures.resize(captive_count);
            for (uint32_t i = 0; i < captive_count; ++i) {
                if (!read_captive_creature(data, offset,
                        chunk.captive_creatures[i], version)) {
                    return false;
                }
            }
        }
    } else {
        chunk.has_captive_creatures = false;
    }

    return true;
}

uint8_t ChunkSerializer::peek_version(const std::vector<uint8_t>& data) {
    if (data.empty()) return 0;
    return data[0];
}

// --- Write helpers ---

void ChunkSerializer::write_uint8(std::vector<uint8_t>& buf, uint8_t value) {
    buf.push_back(value);
}

void ChunkSerializer::write_uint16(std::vector<uint8_t>& buf, uint16_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void ChunkSerializer::write_int32(std::vector<uint8_t>& buf, int32_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void ChunkSerializer::write_uint32(std::vector<uint8_t>& buf, uint32_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void ChunkSerializer::write_int64(std::vector<uint8_t>& buf, int64_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void ChunkSerializer::write_uint64(std::vector<uint8_t>& buf, uint64_t value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void ChunkSerializer::write_float(std::vector<uint8_t>& buf, float value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), bytes, bytes + sizeof(value));
}

void ChunkSerializer::write_string(std::vector<uint8_t>& buf,
                                   const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    write_uint32(buf, len);
    if (len > 0) {
        const auto* data = reinterpret_cast<const uint8_t*>(str.data());
        buf.insert(buf.end(), data, data + len);
    }
}

void ChunkSerializer::write_bytes(std::vector<uint8_t>& buf,
                                  const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

// --- Read helpers ---

bool ChunkSerializer::read_uint8(const std::vector<uint8_t>& data,
                                 size_t& offset, uint8_t& out) {
    if (offset >= data.size()) return false;
    out = data[offset++];
    return true;
}

bool ChunkSerializer::read_uint16(const std::vector<uint8_t>& data,
                                  size_t& offset, uint16_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool ChunkSerializer::read_int32(const std::vector<uint8_t>& data,
                                 size_t& offset, int32_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool ChunkSerializer::read_uint32(const std::vector<uint8_t>& data,
                                  size_t& offset, uint32_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool ChunkSerializer::read_int64(const std::vector<uint8_t>& data,
                                 size_t& offset, int64_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool ChunkSerializer::read_uint64(const std::vector<uint8_t>& data,
                                  size_t& offset, uint64_t& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool ChunkSerializer::read_float(const std::vector<uint8_t>& data,
                                 size_t& offset, float& out) {
    if (offset + sizeof(out) > data.size()) return false;
    std::memcpy(&out, &data[offset], sizeof(out));
    offset += sizeof(out);
    return true;
}

bool ChunkSerializer::read_string(const std::vector<uint8_t>& data,
                                  size_t& offset, std::string& out) {
    uint32_t len;
    if (!read_uint32(data, offset, len)) return false;
    if (offset + len > data.size()) return false;
    out.assign(reinterpret_cast<const char*>(&data[offset]), len);
    offset += len;
    return true;
}

bool ChunkSerializer::read_bytes(const std::vector<uint8_t>& data,
                                 size_t& offset, uint8_t* out, size_t len) {
    if (offset + len > data.size()) return false;
    std::memcpy(out, &data[offset], len);
    offset += len;
    return true;
}

// --- Connector helpers ---

void ChunkSerializer::write_connector(std::vector<uint8_t>& buf,
                                      const ConnectorPlacement& conn) {
    write_uint64(buf, static_cast<uint64_t>(conn.connector_id));
    write_string(buf, conn.from_dimension);
    write_int32(buf, conn.from_cell_x);
    write_int32(buf, conn.from_cell_y);
    write_int32(buf, conn.from_cell_z);
    write_string(buf, conn.to_dimension);
    write_int32(buf, conn.to_cell_x);
    write_int32(buf, conn.to_cell_y);
    write_int32(buf, conn.to_cell_z);
    write_uint8(buf, conn.one_way ? 1 : 0);
    write_uint8(buf, conn.locked ? 1 : 0);
    write_string(buf, conn.connector_type);
    write_uint8(buf, static_cast<uint8_t>(conn.activation_mode));
}

bool ChunkSerializer::read_connector(const std::vector<uint8_t>& data,
                                      size_t& offset,
                                      ConnectorPlacement& conn) {
    uint64_t raw_id;
    if (!read_uint64(data, offset, raw_id)) return false;
    conn.connector_id = static_cast<int64_t>(raw_id);
    if (!read_string(data, offset, conn.from_dimension)) return false;
    if (!read_int32(data, offset, conn.from_cell_x)) return false;
    if (!read_int32(data, offset, conn.from_cell_y)) return false;
    if (!read_int32(data, offset, conn.from_cell_z)) return false;
    if (!read_string(data, offset, conn.to_dimension)) return false;
    if (!read_int32(data, offset, conn.to_cell_x)) return false;
    if (!read_int32(data, offset, conn.to_cell_y)) return false;
    if (!read_int32(data, offset, conn.to_cell_z)) return false;

    uint8_t ow, lk, am;
    if (!read_uint8(data, offset, ow)) return false;
    if (!read_uint8(data, offset, lk)) return false;
    conn.one_way = (ow != 0);
    conn.locked = (lk != 0);

    if (!read_string(data, offset, conn.connector_type)) return false;
    if (!read_uint8(data, offset, am)) return false;
    conn.activation_mode = static_cast<int>(am);

    return true;
}

// --- Mechanism helpers ---

void ChunkSerializer::write_mechanism(
    std::vector<uint8_t>& buf,
    const MechanismPlacement& mechanism) {
    write_string(buf, mechanism.mechanism_id);
    write_string(buf, mechanism.dimension_id);
    write_int32(buf, mechanism.cell_x);
    write_int32(buf, mechanism.cell_y);
    write_int32(buf, mechanism.cell_z);
    write_string(buf, mechanism.title_key);
    write_string(buf, mechanism.action_label);
    write_string(buf, mechanism.flag_name);
    write_uint8(buf, static_cast<uint8_t>(mechanism.activation_mode));
    write_uint8(buf, mechanism.one_shot ? 1 : 0);
    write_string(buf, mechanism.required_flag);

    write_uint32(buf, static_cast<uint32_t>(mechanism.effects.size()));
    for (const auto& effect : mechanism.effects) {
        write_mechanism_effect(buf, effect);
    }
}

bool ChunkSerializer::read_mechanism(
    const std::vector<uint8_t>& data,
    size_t& offset,
    MechanismPlacement& mechanism) {
    if (!read_string(data, offset, mechanism.mechanism_id)) return false;
    if (!read_string(data, offset, mechanism.dimension_id)) return false;
    if (!read_int32(data, offset, mechanism.cell_x)) return false;
    if (!read_int32(data, offset, mechanism.cell_y)) return false;
    if (!read_int32(data, offset, mechanism.cell_z)) return false;
    if (!read_string(data, offset, mechanism.title_key)) return false;
    if (!read_string(data, offset, mechanism.action_label)) return false;
    if (!read_string(data, offset, mechanism.flag_name)) return false;

    uint8_t activation_mode;
    uint8_t one_shot;
    if (!read_uint8(data, offset, activation_mode)) return false;
    if (!read_uint8(data, offset, one_shot)) return false;
    mechanism.activation_mode = static_cast<int>(activation_mode);
    mechanism.one_shot = (one_shot != 0);

    if (!read_string(data, offset, mechanism.required_flag)) return false;

    uint32_t effect_count;
    if (!read_uint32(data, offset, effect_count)) return false;
    mechanism.effects.clear();
    mechanism.effects.reserve(effect_count);
    for (uint32_t i = 0; i < effect_count; ++i) {
        MechanismEffectPlacement effect;
        if (!read_mechanism_effect(data, offset, effect)) return false;
        mechanism.effects.push_back(std::move(effect));
    }

    return true;
}

void ChunkSerializer::write_mechanism_effect(
    std::vector<uint8_t>& buf,
    const MechanismEffectPlacement& effect) {
    write_string(buf, effect.effect_type);
    write_uint64(buf, static_cast<uint64_t>(effect.connector_id));
    write_uint8(buf, effect.when_active ? 1 : 0);
    write_uint8(buf, effect.when_inactive ? 1 : 0);
}

bool ChunkSerializer::read_mechanism_effect(
    const std::vector<uint8_t>& data,
    size_t& offset,
    MechanismEffectPlacement& effect) {
    if (!read_string(data, offset, effect.effect_type)) return false;

    uint64_t connector_id;
    if (!read_uint64(data, offset, connector_id)) return false;
    effect.connector_id = static_cast<int64_t>(connector_id);

    uint8_t when_active;
    uint8_t when_inactive;
    if (!read_uint8(data, offset, when_active)) return false;
    if (!read_uint8(data, offset, when_inactive)) return false;
    effect.when_active = (when_active != 0);
    effect.when_inactive = (when_inactive != 0);

    return true;
}

// --- Block entity helpers ---

void ChunkSerializer::write_block_entity(
    std::vector<uint8_t>& buf,
    const BlockEntityPlacement& entity) {
    write_uint64(buf, entity.id.id);
    write_uint8(buf, static_cast<uint8_t>(entity.entity_type));
    write_int32(buf, entity.root_x);
    write_int32(buf, entity.root_y);
    write_int32(buf, entity.root_z);
    write_string(buf, entity.type_data_json);
    write_uint32(buf, entity.owned_cell_count);
}

bool ChunkSerializer::read_block_entity(
    const std::vector<uint8_t>& data,
    size_t& offset,
    BlockEntityPlacement& entity) {
    uint64_t raw_id;
    if (!read_uint64(data, offset, raw_id)) return false;
    entity.id = EntityId{raw_id};

    uint8_t type_byte;
    if (!read_uint8(data, offset, type_byte)) return false;
    entity.entity_type = static_cast<BlockEntityType>(type_byte);

    if (!read_int32(data, offset, entity.root_x)) return false;
    if (!read_int32(data, offset, entity.root_y)) return false;
    if (!read_int32(data, offset, entity.root_z)) return false;

    if (!read_string(data, offset, entity.type_data_json)) return false;

    if (!read_uint32(data, offset, entity.owned_cell_count)) return false;

    return true;
}

// --- Population cell helpers ---

void ChunkSerializer::write_population_cell(
    std::vector<uint8_t>& buf,
    const PopulationCell& cell) {
    write_float(buf, cell.vegetation_density);
    write_float(buf, cell.herbivore_density);
    write_float(buf, cell.predator_density);
    write_float(buf, cell.soil_fertility);
    write_float(buf, cell.water_availability);
    write_float(buf, cell.dead_biomass);
    write_uint8(buf, cell.biome_type);
    // v7: hunting pressure fields.
    write_float(buf, cell.hunting_pressure_herb);
    write_float(buf, cell.hunting_pressure_pred);
}

bool ChunkSerializer::read_population_cell(
    const std::vector<uint8_t>& data,
    size_t& offset,
    PopulationCell& cell) {
    if (!read_float(data, offset, cell.vegetation_density)) return false;
    if (!read_float(data, offset, cell.herbivore_density)) return false;
    if (!read_float(data, offset, cell.predator_density)) return false;
    if (!read_float(data, offset, cell.soil_fertility)) return false;
    if (!read_float(data, offset, cell.water_availability)) return false;
    if (!read_float(data, offset, cell.dead_biomass)) return false;
    uint8_t biome;
    if (!read_uint8(data, offset, biome)) return false;
    cell.biome_type = biome;
    // v7: hunting pressure fields. If not present (v6), default to 0.
    if (!read_float(data, offset, cell.hunting_pressure_herb)) {
        cell.hunting_pressure_herb = 0.0f;
        return true;
    }
    if (!read_float(data, offset, cell.hunting_pressure_pred)) {
        cell.hunting_pressure_pred = 0.0f;
        return true;
    }
    return true;
}

// --- Captive creature helpers (v8) ---

void ChunkSerializer::write_captive_creature(
    std::vector<uint8_t>& buf,
    const CaptiveCreature& cc) {
    // runtime_id is not persisted (reassigned on load).
    // v9: species_id / partner_species_id 持久化为 string key（P3 存档 key 化）。
    // 通过 CreatureSpeciesRegistry 将 runtime ID 反查为 species_key。
    std::string species_key;
    std::string partner_key;
    const auto& registry = CreatureSpeciesRegistry::staging();
    const CreatureSpeciesDef* def = registry.get_species(cc.species_id);
    if (def) species_key = def->species_key;
    const CreatureSpeciesDef* partner_def = registry.get_species(cc.partner_species_id);
    if (partner_def) partner_key = partner_def->species_key;

    write_string(buf, species_key);
    write_uint8(buf, static_cast<uint8_t>(cc.role));
    write_uint8(buf, static_cast<uint8_t>(cc.age_stage));
    write_float(buf, cc.pos_x);
    write_float(buf, cc.pos_y);
    write_float(buf, cc.pos_z);
    write_float(buf, cc.wander_target_x);
    write_float(buf, cc.wander_target_y);
    write_float(buf, cc.wander_target_z);
    write_int64(buf, cc.next_wander_tick);
    write_int32(buf, cc.bounds_min_x);
    write_int32(buf, cc.bounds_min_y);
    write_int32(buf, cc.bounds_min_z);
    write_int32(buf, cc.bounds_max_x);
    write_int32(buf, cc.bounds_max_y);
    write_int32(buf, cc.bounds_max_z);
    write_float(buf, cc.health);
    write_float(buf, cc.tame_progress);
    write_uint8(buf, cc.is_tamed ? 1 : 0);
    write_int64(buf, cc.capture_tick);
    write_int64(buf, cc.birth_tick);
    write_int64(buf, cc.grow_up_tick);
    write_int64(buf, cc.breed_cooldown_until);
    write_int64(buf, cc.gestation_end_tick);
    write_uint8(buf, cc.is_pregnant ? 1 : 0);
    write_string(buf, partner_key);
}

bool ChunkSerializer::read_captive_creature(
    const std::vector<uint8_t>& data,
    size_t& offset,
    CaptiveCreature& cc,
    uint8_t version) {
    // v8: species_id 持久化为 uint16（legacy，直接使用 runtime ID）
    // v9: species_id 持久化为 string key，加载时通过 registry remap
    if (version >= 9) {
        std::string species_key;
        if (!read_string(data, offset, species_key)) return false;
        if (species_key.empty()) {
            cc.species_id = 0;
        } else {
            const CreatureSpeciesDef* def =
                CreatureSpeciesRegistry::staging().get_species_by_key(species_key);
            cc.species_id = def ? def->species_id : 0;
        }
    } else {
        uint16_t species;
        if (!read_uint16(data, offset, species)) return false;
        cc.species_id = species;
    }

    uint8_t role;
    if (!read_uint8(data, offset, role)) return false;
    cc.role = static_cast<CreatureRole>(role);
    uint8_t age;
    if (!read_uint8(data, offset, age)) return false;
    cc.age_stage = static_cast<CreatureAgeStage>(age);
    if (!read_float(data, offset, cc.pos_x)) return false;
    if (!read_float(data, offset, cc.pos_y)) return false;
    if (!read_float(data, offset, cc.pos_z)) return false;
    if (!read_float(data, offset, cc.wander_target_x)) return false;
    if (!read_float(data, offset, cc.wander_target_y)) return false;
    if (!read_float(data, offset, cc.wander_target_z)) return false;
    if (!read_int64(data, offset, cc.next_wander_tick)) return false;
    if (!read_int32(data, offset, cc.bounds_min_x)) return false;
    if (!read_int32(data, offset, cc.bounds_min_y)) return false;
    if (!read_int32(data, offset, cc.bounds_min_z)) return false;
    if (!read_int32(data, offset, cc.bounds_max_x)) return false;
    if (!read_int32(data, offset, cc.bounds_max_y)) return false;
    if (!read_int32(data, offset, cc.bounds_max_z)) return false;
    if (!read_float(data, offset, cc.health)) return false;
    if (!read_float(data, offset, cc.tame_progress)) return false;
    uint8_t tamed;
    if (!read_uint8(data, offset, tamed)) return false;
    cc.is_tamed = (tamed != 0);
    if (!read_int64(data, offset, cc.capture_tick)) return false;
    if (!read_int64(data, offset, cc.birth_tick)) return false;
    if (!read_int64(data, offset, cc.grow_up_tick)) return false;
    if (!read_int64(data, offset, cc.breed_cooldown_until)) return false;
    if (!read_int64(data, offset, cc.gestation_end_tick)) return false;
    uint8_t pregnant;
    if (!read_uint8(data, offset, pregnant)) return false;
    cc.is_pregnant = (pregnant != 0);

    // v8: partner_species_id 持久化为 uint16（legacy）
    // v9: partner_species_id 持久化为 string key + remap
    if (version >= 9) {
        std::string partner_key;
        if (!read_string(data, offset, partner_key)) return false;
        if (partner_key.empty()) {
            cc.partner_species_id = 0;
        } else {
            const CreatureSpeciesDef* partner_def =
                CreatureSpeciesRegistry::staging().get_species_by_key(partner_key);
            cc.partner_species_id = partner_def ? partner_def->species_id : 0;
        }
    } else {
        uint16_t partner;
        if (!read_uint16(data, offset, partner)) return false;
        cc.partner_species_id = partner;
    }
    return true;
}

} // namespace science_and_theology
