// Serializer<T>: per-type binary serialization template.
//
// Design rationale:
//   - The scene system needs to serialize arbitrary components without
//     knowing their types at the call site (e.g. "serialize all
//     components on entity E"). A central template + per-type
//     specializations give that dispatch while keeping the per-type
//     code next to each component's definition.
//   - We use explicit static-method template specialization (NOT ADL
//     free functions like nlohmann_json's to_json/from_json). The
//     explicit form:
//       1. Makes it impossible to silently forget a specialization
//          (unspecialized -> static_assert fires at compile time).
//       2. Makes the registry's job easy: `save_component` just calls
//          Serializer<T>::write(writer, value) for each registered T.
//   - The read/write signatures match BinaryWriter/BinaryReader's
//     primitive methods (write returns void, read returns bool).
//
// Usage for a new component:
//   1. Define the component struct (e.g. struct Velocity { float x,y,z; };).
//   2. Specialize Serializer<Velocity> in the same header as Velocity
//      (or in component_serializer.cpp if you prefer co-locating impls):
//        template <>
//        struct Serializer<Velocity> {
//            static void write(BinaryWriter& w, const Velocity& v) {
//                w.write_f32(v.x); w.write_f32(v.y); w.write_f32(v.z);
//            }
//            static bool read(BinaryReader& r, Velocity& v) {
//                return r.read_f32(v.x) && r.read_f32(v.y) && r.read_f32(v.z);
//            }
//        };
//   3. Register it with ComponentRegistry (see ecs/component_registry.h)
//      so the scene system can serialize it by type name.
//
// Versioning: per-type serializers do NOT version themselves. File-level
// versioning lives in the file header (see SceneFileHeader). When a
// component's layout changes, bump the scene file version and add a
// versioned read path: `if (file_version >= 2) r.read_f32(v.new_field);`.
//
// Layering: lives in core/ so both ecs/ (components) and scene/ can
// include it. No deps beyond binary_writer.h / binary_reader.h.

#pragma once

#include "core/binary_reader.h"
#include "core/binary_writer.h"

#include <type_traits>

namespace snt::core {

// Primary template — deliberately left undefined. Any type without a
// specialization will trigger the static_assert below at the point of
// use (instantiation), pointing the caller at the missing specialization.
template <typename T>
struct Serializer {
    static_assert(!std::is_same_v<T, T>,
                  "Serializer<T> is not specialized for this type. "
                  "Add a specialization with write(BinaryWriter&, const T&) "
                  "and read(BinaryReader&, T&) static methods.");
};

}  // namespace snt::core
