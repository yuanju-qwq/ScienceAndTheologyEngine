// EnTT configuration wrapper.
//
// EnTT's config/config.h checks if ENTT_ASSERT is user-defined. If not,
// it falls back to `#include <cassert>` + `assert(condition)`. We prefer
// to route EnTT assertions through our own SNT_LOG_FATAL + SNT_DEBUGBREAK
// path so failures show up in the engine log and break into the debugger
// consistently with SNT_ASSERT.
//
// Rule: every file that needs EnTT MUST include this wrapper instead of
// <entt/entt.hpp> directly, so the assertion hook is always installed
// before EnTT's config.h runs.
//
// Note: <cassert> is included here so GLM and other third-party headers
// that call `assert(...)` can find the standard macro. (Our own
// engine/core/snt_assert.h deliberately does NOT shadow the standard
// assert.h — it is named snt_assert.h to avoid include-path collisions
// when -I engine/core precedes the UCRT include path.)

#pragma once

#include <cassert>

#include "core/snt_assert.h"  // SNT_DEBUGBREAK
#include "core/log.h"     // SNT_LOG_FATAL

#include <cstdlib>  // std::abort

#ifndef ENTT_ASSERT
#  define ENTT_ASSERT(condition, msg)                                        \
      do {                                                                  \
          if (!(condition)) {                                              \
              SNT_LOG_FATAL("EnTT assertion failed: %s", #condition);       \
              SNT_LOG_FATAL("  %s", msg);                                  \
              SNT_DEBUGBREAK();                                             \
              std::abort();                                                 \
          }                                                                 \
      } while (0)
#endif

#include <entt/entt.hpp>
