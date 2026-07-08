// BinaryReader: bounds-checked byte reader for deserializing engine data.
//
// Design rationale:
//   - Paired with BinaryWriter (same byte order, same length-prefix
//     conventions). See binary_writer.h for the overall rationale.
//   - Read methods return bool: false on EOF / truncated input. This
//     matches src/core/save/chunk_serializer.hpp's read helpers so the
//     new engine stays consistent with the legacy save system.
//   - Bounds-checked: every read validates there are enough bytes left.
//     A false return lets the caller abort gracefully (e.g. mark the
//     save file as corrupt) instead of UB / segfault.
//   - Non-owning: the reader borrows a byte buffer from the caller
//     (typically loaded from disk via core::read_file). The caller must
//     keep the buffer alive for the reader's lifetime.
//
// Versioning: readers expect a 4-byte magic + 4-byte version at the
// start (see SceneFileHeader in scene/scene.h). The caller is expected
// to read the header first and bail if the version is unsupported;
// read_u32() etc. can then be used to parse the payload.
//
// Layering: lives in core/ (no deps beyond <cstdint>/<vector>/<string>).

#pragma once

#include <cstdint>
#include <cstring>   // std::memcpy
#include <string>
#include <string_view>
#include <vector>

namespace snt::core {

// Bounds-checked binary reader. Borrows the buffer; does not own it.
class BinaryReader {
public:
    BinaryReader() = default;

    // Construct from a borrowed byte buffer. The caller must keep `data`
    // alive for the reader's lifetime.
    explicit BinaryReader(const std::vector<uint8_t>& data)
        : data_(data.data()), size_(data.size()), cursor_(data.data()) {}

    BinaryReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), cursor_(data) {}

    // Non-copyable (borrows state); movable.
    BinaryReader(const BinaryReader&) = delete;
    BinaryReader& operator=(const BinaryReader&) = delete;
    BinaryReader(BinaryReader&&) noexcept = default;
    BinaryReader& operator=(BinaryReader&&) noexcept = default;

    // --- Primitive readers ---
    // All reads are bounds-checked: return false on truncation.
    // On false, the output value is left unchanged (caller's original).

    bool read_u8(uint8_t& out)  { return read_raw(&out, 1); }
    bool read_i8(int8_t& out)  { return read_u8(reinterpret_cast<uint8_t&>(out)); }

    bool read_u16(uint16_t& out) { return read_raw(&out, sizeof(out)); }
    bool read_i16(int16_t& out)  { return read_u16(reinterpret_cast<uint16_t&>(out)); }

    bool read_u32(uint32_t& out) { return read_raw(&out, sizeof(out)); }
    bool read_i32(int32_t& out)  { return read_u32(reinterpret_cast<uint32_t&>(out)); }

    bool read_u64(uint64_t& out) { return read_raw(&out, sizeof(out)); }
    bool read_i64(int64_t& out)  { return read_u64(reinterpret_cast<uint64_t&>(out)); }

    bool read_f32(float& out)    { return read_raw(&out, sizeof(out)); }
    bool read_f64(double& out)   { return read_raw(&out, sizeof(out)); }

    // Read a length-prefixed string (u32 length + raw bytes). Matches
    // BinaryWriter::write_string. Returns false on truncation or if
    // the declared length exceeds the remaining bytes (corrupt input).
    bool read_string(std::string& out) {
        uint32_t len = 0;
        if (!read_u32(len)) return false;
        if (len > remaining()) return false;  // corrupt: length overflows
        out.assign(reinterpret_cast<const char*>(cursor_), len);
        cursor_ += len;
        return true;
    }

    // Read a raw byte block into a caller-provided buffer.
    bool read_raw(void* out, size_t size) {
        if (remaining() < size) return false;
        std::memcpy(out, cursor_, size);
        cursor_ += size;
        return true;
    }

    // --- Cursor state ---

    size_t position() const { return static_cast<size_t>(cursor_ - data_); }
    size_t remaining() const { return size_ - position(); }
    bool eof() const { return position() >= size_; }

    // Reset the cursor to the start (e.g. re-read for a second pass).
    void rewind() { cursor_ = data_; }

    // Skip `n` bytes without reading them (e.g. skip an unknown field
    // in a forward-compatible reader). Returns false on truncation.
    bool skip(size_t n) {
        if (remaining() < n) return false;
        cursor_ += n;
        return true;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    const uint8_t* cursor_ = nullptr;
};

}  // namespace snt::core
