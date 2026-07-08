// Engine-wide assertion macros.
//
// IMPORTANT: this file is named snt_assert.h (NOT assert.h) on purpose.
// The engine's CMake setup adds -I engine/core to the include path, which
// sits before the UCRT system include path. If this file were named
// assert.h, `#include <assert.h>` from third-party headers (GLM, etc.)
// would resolve to THIS file instead of the standard UCRT assert.h,
// breaking the standard `assert` macro. Do NOT rename this file back to
// assert.h.
//
// Design goals:
//   - In Debug: log a FATAL message via SNT_LOG_FATAL, then __debugbreak()
//     + abort(). The debugger stops exactly at the failure site, with the
//     message already on stderr.
//   - In Release (NDEBUG defined): SNT_ASSERT / SNT_ASSERT_MSG are compiled
//     out completely — zero cost in shipping builds.
//   - SNT_VERIFY: like SNT_ASSERT but the condition is evaluated in both
//     Debug and Release. Use when the expression has a side effect that
//     must run (e.g. `SNT_VERIFY(vkQueueWaitIdle(device_) == VK_SUCCESS)`).
//   - SNT_UNREACHABLE: marks code paths that must never execute. Logs +
//     aborts in both Debug and Release (cannot be compiled out).
//
// Usage:
//   SNT_ASSERT(device_ != nullptr, "device_ must be initialized");
//   SNT_ASSERT(index < size_, "index %u out of range [0, %u)", index, size_);
//   SNT_VERIFY(vkCreateDevice(...) == VK_SUCCESS);
//   default:
//       SNT_UNREACHABLE();  // unreachable switch case
//
// Note: SNT_ASSERT uses a variadic `fmt, ...` so you can include formatted
// context. The fmt string is printf-style (matches SNT_LOG_*).
//
// Header-only. No .cpp counterpart; safe to include from anywhere.

#pragma once

#include "core/log.h"  // SNT_LOG_FATAL

#include <cstdlib>  // std::abort

// ---------------------------------------------------------------------------
// Platform debugger breakpoint.
// ---------------------------------------------------------------------------
// MSVC: __debugbreak() is a compiler intrinsic that emits an int3.
// GCC / Clang: __builtin_trap() aborts but does not stop the debugger at
// the failure site; we use it as the cross-platform fallback.
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
#  define SNT_DEBUGBREAK() __debugbreak()
#else
#  define SNT_DEBUGBREAK() __builtin_trap()
#endif

// ---------------------------------------------------------------------------
// SNT_ASSERT / SNT_ASSERT_MSG
// ---------------------------------------------------------------------------
// Compiles out in Release (NDEBUG defined). The `fmt` argument is a
// printf-style format string; additional variadic args are forwarded to
// SNT_LOG_FATAL.
//
// Implementation note: we wrap the macro body in `do { ... } while (0)` so
// it behaves like a single statement (e.g. inside `if (...) SNT_ASSERT(...);`).
// ---------------------------------------------------------------------------
#ifdef NDEBUG
#  define SNT_ASSERT(cond, ...) ((void)0)
#  define SNT_ASSERT_MSG(cond, msg) ((void)0)
#else
#  define SNT_ASSERT(cond, ...)                                                  \
      do {                                                                       \
          if (!(cond)) {                                                         \
              SNT_LOG_FATAL("Assertion failed: %s", #cond);                      \
              SNT_LOG_FATAL("  " __VA_ARGS__);                                   \
              SNT_DEBUGBREAK();                                                  \
              std::abort();                                                      \
          }                                                                      \
      } while (0)
#  define SNT_ASSERT_MSG(cond, msg)                                              \
      do {                                                                       \
          if (!(cond)) {                                                         \
              SNT_LOG_FATAL("Assertion failed: %s", #cond);                      \
              SNT_LOG_FATAL("  %s", msg);                                        \
              SNT_DEBUGBREAK();                                                  \
              std::abort();                                                      \
          }                                                                      \
      } while (0)
#endif

// ---------------------------------------------------------------------------
// SNT_VERIFY
// ---------------------------------------------------------------------------
// Like SNT_ASSERT but the condition is ALWAYS evaluated. In Debug, a
// failed SNT_VERIFY triggers the same debugbreak + abort. In Release, the
// condition is evaluated but its result is discarded — use this when the
// condition has side effects you need in both configs (e.g. `SNT_VERIFY(vkDeviceWaitIdle(device_) == VK_SUCCESS)`).
// ---------------------------------------------------------------------------
#ifdef NDEBUG
#  define SNT_VERIFY(cond, ...) ((void)(cond))
#else
#  define SNT_VERIFY(cond, ...)                                                  \
      do {                                                                       \
          if (!(cond)) {                                                         \
              SNT_LOG_FATAL("Verify failed: %s", #cond);                          \
              SNT_LOG_FATAL("  " __VA_ARGS__);                                   \
              SNT_DEBUGBREAK();                                                  \
              std::abort();                                                      \
          }                                                                      \
      } while (0)
#endif

// ---------------------------------------------------------------------------
// SNT_UNREACHABLE
// ---------------------------------------------------------------------------
// Marks a code path that must never execute. Logs + aborts in BOTH Debug
// and Release — never compiles out. Use for `default:` cases that should
// be impossible, or after a `return` that exhausts all paths.
// ---------------------------------------------------------------------------
#define SNT_UNREACHABLE()                                                        \
    do {                                                                         \
        SNT_LOG_FATAL("Unreachable code path hit");                              \
        SNT_DEBUGBREAK();                                                        \
        std::abort();                                                            \
    } while (0)

// ---------------------------------------------------------------------------
// SNT_STATIC_ASSERT
// ---------------------------------------------------------------------------
// Thin wrapper over static_assert for documentation parity with SNT_ASSERT.
// Always evaluated (compile-time); produces a clear error message.
// ---------------------------------------------------------------------------
#define SNT_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
