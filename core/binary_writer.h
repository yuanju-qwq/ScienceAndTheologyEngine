// BinaryWriter: append-only byte buffer for serializing engine data.
//
// Design rationale:
//   - Scene files, save files, and chunk data are binary (not JSON) per
//     project_memory: "JSON for config, binary for large data".
//   - This writer mirrors the style of src/core/save/chunk_serializer.hpp
//     (static helpers, host byte order, length-prefixed strings) so the
//     new engine stays consistent with the legacy save system.
//   - Host byte order = little-endian on x86/ARM64 (all target platforms).
//     No byte-swapping is done; files are NOT portable across big-endian
//     machines (acceptable: the engine targets x64 + ARM64 Windows/Linux).
//   - Write methods are infallible: they push bytes to a vector that
//     grows on demand. Out-of-memory is the only failure mode and is
//     allowed to terminate (consistent with std::vector behavior).
//
// Versioning: each file format begins with a 4-byte magic + 4-byte
// version (see SceneFileHeader in scene/scene.h). The reader rejects
// unknown versions. Bump the version when the on-disk layout changes;
// readers handle one version at a time (no partial upgrade).
//
// Layering: lives in core/ (no deps beyond <cstdint>/<vector>/<string>).
// Used by ecs/ (ComponentSerializer), scene/ (Scene), and future
// chunk/ save code.

#pragma once

#include <cstdint>
#include <cstring>   // std::memcpy
#include <string>
#include <string_view>
#include <vector>

namespace snt::core {

// Append-only binary buffer. Writes always succeed (grow as needed).
class BinaryWriter {
public:
    BinaryWriter() = default;

    // Non-copyable (owns the buffer); movable.
    BinaryWriter(const BinaryWriter&) = delete;
    BinaryWriter& operator=(const BinaryWriter&) = delete;
    BinaryWriter(BinaryWriter&&) noexcept = default;
    BinaryWriter& operator=(BinaryWriter&&) noexcept = default;

    // --- Primitive writers ---
    // All multi-byte values are written in host byte order (LE on
    // target platforms). No swapping.

    void write_u8(uint8_t v)  { buffer_.push_back(v); }
    void write_i8(int8_t v)   { write_u8(static_cast<uint8_t>(v)); }

    void write_u16(uint16_t v) { write_raw(&v, sizeof(v)); }
    void write_i16(int16_t v)  { write_u16(static_cast<uint16_t>(v)); }

    void write_u32(uint32_t v) { write_raw(&v, sizeof(v)); }
    void write_i32(int32_t v)  { write_u32(static_cast<uint32_t>(v)); }

    void write_u64(uint64_t v) { write_raw(&v, sizeof(v)); }
    void write_i64(int64_t v)  { write_u64(static_cast<uint64_t>(v)); }

    void write_f32(float v)    { write_raw(&v, sizeof(v)); }
    void write_f64(double v)   { write_raw(&v, sizeof(v)); }

    // Write a length-prefixed UTF-8 string: u32 length + raw bytes.
    // Length is the byte count (not codepoint count). No terminator.
    // This matches src/core/save/save_manager.hpp's write_string style.
    void write_string(std::string_view s) {
        write_u32(static_cast<uint32_t>(s.size()));
        if (!s.empty()) {
            write_raw(s.data(), s.size());
        }
    }

    // Write a raw byte block. Used for fixed-size arrays (e.g. float[3]
    // in Transform: write_raw(t.position, sizeof(t.position))).
    void write_raw(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + size);
    }

    // --- Buffer access ---

    const std::vector<uint8_t>& buffer() const { return buffer_; }
    std::vector<uint8_t>&& take_buffer() && { return std::move(buffer_); }

    size_t size() const { return buffer_.size(); }
    bool empty() const { return buffer_.empty(); }

    // Reset to empty. Useful for reusing the writer across scenes
    // without reallocating.
    void clear() { buffer_.clear(); }

private:
    std::vector<uint8_t> buffer_;
};

}  // namespace snt::core
