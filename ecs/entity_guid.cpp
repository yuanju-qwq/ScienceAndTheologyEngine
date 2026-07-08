// EntityGuid implementation.

#include "ecs/entity_guid.h"

#include "core/binary_reader.h"
#include "core/binary_writer.h"

#include <chrono>
#include <random>

namespace snt::ecs {

namespace {

// Mix two 32-bit values into one via xorshift-ish twiddle. Cheap and
// good enough for Guid seed uniqueness within a single process run.
uint32_t mix_seed(uint32_t a, uint32_t b) {
    uint32_t x = a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
    x ^= x >> 17;
    x *= 0xed5ad4bbu;
    x ^= x >> 11;
    x *= 0xac4c1b51u;
    x ^= x >> 15;
    x *= 0x31848babu;
    x ^= x >> 14;
    return x;
}

}  // namespace

EntityGuidGenerator::EntityGuidGenerator() {
    // Seed = hash of steady_clock nanos + a per-instance random_device draw.
    // Two generators constructed in the same process will (with extremely
    // high probability) get different seeds, so their Guids never collide.
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device rd;
    seed_ = (static_cast<uint64_t>(mix_seed(
                 static_cast<uint32_t>(now),
                 rd())) << 32) |
            static_cast<uint64_t>(mix_seed(
                 static_cast<uint32_t>(now >> 32),
                 rd()));
    // Guard against the astronomically unlikely case of seed == 0 (which
    // would make the first Guid collide with kInvalidEntityGuid on
    // counter==0; we already skip counter 0 in next(), but keeping the
    // seed nonzero keeps the Guid space uniform).
    if (seed_ == 0) {
        seed_ = 1;
    }
}

EntityGuid EntityGuidGenerator::next() {
    ++counter_;  // skip 0; first issued Guid uses counter=1
    return pack();
}

EntityGuid EntityGuidGenerator::peek_next() const {
    // next() will pre-increment counter_, so the next issued Guid packs
    // (counter_ + 1).
    EntityGuidGenerator copy = *this;
    return copy.next();
}

void EntityGuidGenerator::reset_counter(uint32_t first) {
    counter_ = first;
}

EntityGuid EntityGuidGenerator::pack() const {
    // Layout: high 32 bits = seed, low 32 bits = counter.
    return EntityGuid{(seed_ << 32) | static_cast<uint64_t>(counter_)};
}

}  // namespace snt::ecs

// ---------------------------------------------------------------------------
// Serializer<EntityGuid> implementation.
// ---------------------------------------------------------------------------
// Defined out-of-line to keep entity_guid.h dependency-light (it only
// forward-declares BinaryWriter / BinaryReader). The .cpp pulls in the
// full definitions.
namespace snt::core {

void Serializer<snt::ecs::EntityGuid>::write(
    BinaryWriter& w, const snt::ecs::EntityGuid& g) {
    w.write_u64(g.value);
}

bool Serializer<snt::ecs::EntityGuid>::read(
    BinaryReader& r, snt::ecs::EntityGuid& g) {
    return r.read_u64(g.value);
}

}  // namespace snt::core
