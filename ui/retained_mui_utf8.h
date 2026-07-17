// Retained-MUI private UTF-8 boundary utilities.
//
// Text shaping and editable controls use the same byte-boundary rules. Keep
// them here so cursor editing cannot diverge from the shaping input path.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace snt::ui::detail {

inline uint32_t decode_utf8(std::string_view text, size_t& offset) {
    const auto read = [&](size_t index) -> uint8_t {
        return static_cast<uint8_t>(text[offset + index]);
    };

    if (offset >= text.size()) return 0;

    const uint8_t first = read(0);
    if (first < 0x80u) {
        ++offset;
        return first;
    }
    if ((first & 0xE0u) == 0xC0u && offset + 1 < text.size()) {
        const uint32_t codepoint = ((first & 0x1Fu) << 6) | (read(1) & 0x3Fu);
        offset += 2;
        return codepoint;
    }
    if ((first & 0xF0u) == 0xE0u && offset + 2 < text.size()) {
        const uint32_t codepoint = ((first & 0x0Fu) << 12) |
                                   ((read(1) & 0x3Fu) << 6) |
                                   (read(2) & 0x3Fu);
        offset += 3;
        return codepoint;
    }
    if ((first & 0xF8u) == 0xF0u && offset + 3 < text.size()) {
        const uint32_t codepoint = ((first & 0x07u) << 18) |
                                   ((read(1) & 0x3Fu) << 12) |
                                   ((read(2) & 0x3Fu) << 6) |
                                   (read(3) & 0x3Fu);
        offset += 4;
        return codepoint;
    }

    ++offset;
    return 0xFFFDu;
}

inline bool utf8_continuation(unsigned char value) {
    return (value & 0xC0u) == 0x80u;
}

inline size_t utf8_previous_boundary(std::string_view text, size_t offset) {
    offset = std::min(offset, text.size());
    if (offset == 0) return 0;
    --offset;
    while (offset > 0 && utf8_continuation(static_cast<unsigned char>(text[offset]))) {
        --offset;
    }
    return offset;
}

inline size_t utf8_next_boundary(std::string_view text, size_t offset) {
    offset = std::min(offset, text.size());
    if (offset >= text.size()) return text.size();
    ++offset;
    while (offset < text.size() &&
           utf8_continuation(static_cast<unsigned char>(text[offset]))) {
        ++offset;
    }
    return offset;
}

inline size_t utf8_boundary_at_or_before(std::string_view text, size_t offset) {
    offset = std::min(offset, text.size());
    while (offset > 0 && offset < text.size() &&
           utf8_continuation(static_cast<unsigned char>(text[offset]))) {
        --offset;
    }
    return offset;
}

inline std::string utf8_mask(std::string_view text) {
    std::string result;
    size_t offset = 0;
    while (offset < text.size()) {
        static_cast<void>(decode_utf8(text, offset));
        result.push_back('*');
    }
    return result;
}

}  // namespace snt::ui::detail
