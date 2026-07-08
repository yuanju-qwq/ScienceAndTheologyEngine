// Pipe network type definitions.
//
// Ported from src/core/network/pipe_types.hpp.
// Namespace: science_and_theology::gt -> snt::data (gt sub-namespace merged
// into snt::data per P2 decision).

#pragma once

#include <cstdint>

namespace snt::data {

// Type of pipe network. Determines what can be transported.
// Used by both FluidNetwork (LIQUID/GAS) and ItemPipeNetwork (ITEM).
enum class PipeType : uint8_t {
    LIQUID = 0,
    GAS = 1,
    ITEM = 2,
};

// Default throughput values per pipe type (items or mB per tick).
inline constexpr int64_t kDefaultThroughput(PipeType type) {
    switch (type) {
        case PipeType::LIQUID: return 100;   // 100 mB/tick
        case PipeType::GAS:    return 200;   // 200 mB/tick (gases flow faster)
        case PipeType::ITEM:   return 1;     // 1 stack per tick
    }
    return 100;
}

} // namespace snt::data
