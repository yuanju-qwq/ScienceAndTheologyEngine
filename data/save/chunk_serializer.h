#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../defs/chunk_data.h"

namespace snt::data {

// Binary serializer / deserializer for a single ChunkData.
// Format is forward-compatible via a version byte.
// All multi-byte integers are stored in host byte order (little-endian).
//
// Thread-safe: all methods are stateless and reentrant.
class ChunkSerializer {
public:
    // Current binary format version.
    // v7: adds hunting_pressure_herb/pred to PopulationCell.
    // v6: adds PopulationCell (ecosystem) after block_entities.
    // v8: adds captive_creatures (husbandry) after population_cell.
    // v9: captive_creatures species_id/partner_species_id 改为 string key
    //     持久化（P3: 存档 key 化，热重载后通过 registry remap 回 runtime ID）。
    static constexpr uint8_t kCurrentVersion = 9;

    // Serializes a chunk to raw bytes. Returns empty vector on failure.
    // The chunk's dimension_id is embedded as part of the serialized data.
    static std::vector<uint8_t> serialize(
        const std::string& dimension_id, const ChunkData& chunk);

    // Deserializes raw bytes back into a ChunkData and its dimension_id.
    // Returns true on success. The caller must validate the version.
    static bool deserialize(
        const std::vector<uint8_t>& data,
        std::string& dimension_id, ChunkData& chunk);

    // Returns the version byte from serialized data without full parsing.
    // Returns 0 if the data is too short or corrupted.
    static uint8_t peek_version(const std::vector<uint8_t>& data);

private:
    // --- Write helpers ---

    static void write_uint8(std::vector<uint8_t>& buf, uint8_t value);
    static void write_uint16(std::vector<uint8_t>& buf, uint16_t value);
    static void write_int32(std::vector<uint8_t>& buf, int32_t value);
    static void write_uint32(std::vector<uint8_t>& buf, uint32_t value);
    static void write_int64(std::vector<uint8_t>& buf, int64_t value);
    static void write_uint64(std::vector<uint8_t>& buf, uint64_t value);
    static void write_float(std::vector<uint8_t>& buf, float value);
    static void write_string(std::vector<uint8_t>& buf,
                             const std::string& str);
    static void write_bytes(std::vector<uint8_t>& buf,
                            const uint8_t* data, size_t len);

    // --- Read helpers ---

    static bool read_uint8(const std::vector<uint8_t>& data,
                           size_t& offset, uint8_t& out);
    static bool read_uint16(const std::vector<uint8_t>& data,
                            size_t& offset, uint16_t& out);
    static bool read_int32(const std::vector<uint8_t>& data,
                           size_t& offset, int32_t& out);
    static bool read_uint32(const std::vector<uint8_t>& data,
                            size_t& offset, uint32_t& out);
    static bool read_int64(const std::vector<uint8_t>& data,
                           size_t& offset, int64_t& out);
    static bool read_uint64(const std::vector<uint8_t>& data,
                            size_t& offset, uint64_t& out);
    static bool read_float(const std::vector<uint8_t>& data,
                           size_t& offset, float& out);
    static bool read_string(const std::vector<uint8_t>& data,
                            size_t& offset, std::string& out);
    static bool read_bytes(const std::vector<uint8_t>& data,
                           size_t& offset, uint8_t* out, size_t len);

    // --- Connector serialization ---

    static void write_connector(std::vector<uint8_t>& buf,
                                 const ConnectorPlacement& conn);
    static bool read_connector(const std::vector<uint8_t>& data,
                               size_t& offset, ConnectorPlacement& conn);

    // --- Mechanism serialization ---

    static void write_mechanism(std::vector<uint8_t>& buf,
                                const MechanismPlacement& mechanism);
    static bool read_mechanism(const std::vector<uint8_t>& data,
                               size_t& offset, MechanismPlacement& mechanism);
    static void write_mechanism_effect(
        std::vector<uint8_t>& buf,
        const MechanismEffectPlacement& effect);
    static bool read_mechanism_effect(
        const std::vector<uint8_t>& data,
        size_t& offset,
        MechanismEffectPlacement& effect);

    // --- Block entity serialization ---

    static void write_block_entity(std::vector<uint8_t>& buf,
                                    const BlockEntityPlacement& entity);
    static bool read_block_entity(const std::vector<uint8_t>& data,
                                  size_t& offset,
                                  BlockEntityPlacement& entity);

    // --- Population cell serialization ---

    static void write_population_cell(std::vector<uint8_t>& buf,
                                       const PopulationCell& cell);
    static bool read_population_cell(const std::vector<uint8_t>& data,
                                     size_t& offset,
                                     PopulationCell& cell);

    // --- Captive creature serialization (v8/v9) ---
    // v8: species_id / partner_species_id 持久化为 uint16（legacy）
    // v9: 持久化为 string key，加载时通过 CreatureSpeciesRegistry remap

    static void write_captive_creature(std::vector<uint8_t>& buf,
                                       const CaptiveCreature& cc);
    // version 参数用于区分 v8（uint16）与 v9（string key + remap）格式。
    static bool read_captive_creature(const std::vector<uint8_t>& data,
                                      size_t& offset,
                                      CaptiveCreature& cc,
                                      uint8_t version);
};

} // namespace science_and_theology
