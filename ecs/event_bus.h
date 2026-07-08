// EventBus: thin alias over entt::dispatcher + convenience helpers.
//
// Why entt::dispatcher?
//   - Already in the dependency tree (EnTT is used for ECS).
//   - Type-safe publish/subscribe via templates.
//   - enqueue + update model supports batching (queue many events, flush
//     once per frame) which avoids recursive dispatch surprises.
//   - Free function sink<Event>() API is concise at call sites.
//
// Lifecycle:
//   1. Engine creates an EventBus instance and passes &bus to subsystems
//      that need to subscribe (InputSystem, CameraSystem, future UI /
//      script systems).
//   2. Subscribers call bus.sink<Event>().connect<&Method>(this) during
//      their init.
//   3. Producers call bus.enqueue<Event>({...}) then bus.update() to
//      dispatch.
//   4. Engine::shutdown calls bus.clear() before subsystems are destroyed
//      so dangling subscriber callbacks never fire.
//
// Header-only. Include from anywhere; pulls entt via ecs/entt_config.h
// (which routes EnTT assertions through SNT_LOG_FATAL).

#pragma once

#include "core/events.h"      // Event structs (POD, no entt dependency)
#include "ecs/entt_config.h"  // entt::dispatcher + assertion hooks

namespace snt::ecs {

// Process-wide type for the event bus. Using a typedef (not a wrapper
// class) so all entt::dispatcher API (sink, enqueue, trigger, update,
// clear) is available without forwarding boilerplate.
using EventBus = entt::dispatcher;

}  // namespace snt::ecs
