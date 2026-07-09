#pragma once

#include <cstdint>
#include <string>

namespace snt::data {

// Result for safe region compaction. Compaction never deletes unique chunk
// entries. It only rewrites region files and removes duplicate entries for the
// same local chunk coordinate, keeping the last entry.
struct RegionCompactionResult {
    bool ok = true;
    int32_t region_files_scanned = 0;
    int32_t region_files_compacted = 0;
    int32_t chunk_entries_before = 0;
    int32_t chunk_entries_after = 0;
    int32_t duplicate_entries_removed = 0;
    int32_t failed_region_files = 0;
};

class RegionCompactor {
public:
    // Rewrite a single region file in compact canonical form.
    // Returns ok=true when the file is absent, already compact, or rewritten.
    static RegionCompactionResult compact_region(
        const std::string& planet_dir,
        const std::string& dimension_id,
        int region_x, int region_y, int region_z);

    // Scan {planet_dir}/regions and compact every region matching dimension_id.
    static RegionCompactionResult compact_dimension(
        const std::string& planet_dir,
        const std::string& dimension_id);
};

} // namespace science_and_theology
