// Lightweight logging system.
//
// Design goals:
//   - 6 severity levels (Trace/Debug/Info/Warn/Error/Fatal) with runtime
//     filtering via set_level(). Messages below the threshold are dropped
//     before formatting, so disabled debug logs have near-zero cost.
//   - Module-scoped channels: each translation unit declares
//         #define SNT_LOG_CHANNEL "render_backend"
//     at the top (before including log.h) and call sites use
//         SNT_LOG_INFO("GPU: %s", name);
//     The macro prepends SNT_LOG_CHANNEL automatically.
//   - Single stderr sink with ANSI color. File/rotating sinks are a future
//     extension point (Sink interface kept private for now).
//   - printf-style format (drop-in replacement for existing fprintf calls).
//
// Usage:
//   // In a .cpp that wants to log:
//   #define SNT_LOG_CHANNEL "render_backend"
//   #include "core/log.h"
//   ...
//   SNT_LOG_INFO("GPU: %s (score %d)", name, score);
//   SNT_LOG_ERROR("vkCreateDevice failed: %d", res);
//
//   // In Engine::init() (once, before any logging):
//   snt::core::Logger::instance().set_level(LogLevel::kInfo);

#pragma once

#include <cassert>     // assert (third-party deps like EnTT assume this is included)
#include <cstdarg>
#include <cstdint>

namespace snt::core {

// Log severity levels, ordered from most verbose to most severe.
// Filtering rule: a message at level L is emitted only if L >= min_level.
enum class LogLevel : int {
    kTrace = 0,  // Very detailed flow tracing (per-frame, per-entity).
    kDebug = 1,  // Diagnostic info useful during development.
    kInfo  = 2,  // High-level lifecycle events (default threshold).
    kWarn  = 3,  // Recoverable but suspicious conditions.
    kError = 4,  // Operation failed but engine keeps running.
    kFatal = 5,  // Unrecoverable; engine cannot continue.
};

// Logger: singleton with a single stderr sink + ANSI color.
//
// Thread-safety: writes are serialized via an internal mutex, so the macros
// are safe to call from worker threads. Performance is adequate for typical
// engine logging volume (hundreds of lines per frame); for high-volume
// per-entity tracing, prefer keeping the default level at kInfo.
//
// File sink: add_file_sink(path) opens an additional sink that receives
// the same log lines (without ANSI color codes, so the file is greppable).
// Engine::init calls this to mirror logs to logs/engine.log. If the file
// can't be opened, the stderr sink still works — logging is best-effort.
class Logger {
public:
    // Access the global Logger instance.
    static Logger& instance();

    // Set the minimum level that will be emitted. Messages below this
    // level are dropped before formatting. Default: kInfo.
    void set_level(LogLevel level);
    LogLevel level() const;

    // Open an additional file sink at `path`. Subsequent log lines are
    // written to BOTH stderr and the file. The file is opened in append
    // mode so restarts preserve history. Returns false if the file
    // could not be opened (logging continues to stderr only).
    //
    // Rotation: when the file exceeds `max_size` bytes, it's renamed to
    // `<path>.1` (and `.1` -> `.2`, `.2` -> `.3`, ...). At most
    // `max_files` rotated copies are kept; the oldest is deleted. This
    // bounds total disk usage at ~(max_files+1) * max_size bytes.
    // Rotation is checked every 1024 writes (amortized cost ~0).
    //
    // On open: if an existing file at `path` already exceeds max_size,
    // it's rotated immediately so the new run starts with a fresh file.
    //
    // Thread-safe: may be called from any thread. The file sink is
    // closed automatically when the Logger is destroyed (process exit).
    bool add_file_sink(const char* path,
                       size_t max_size = 5 * 1024 * 1024,  // 5 MB
                       int    max_files = 3);

    // Emit one log line. Called by the SNT_LOG_* macros; users should
    // not call this directly.
    //   level   - severity
    //   channel - module name (e.g. "render_backend"); may be nullptr
    //   fmt     - printf-style format string
    // GCC/Clang-only: format-string checking via __attribute__. MSVC has no
    // equivalent; the attribute is silently skipped there.
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    void log(LogLevel level, const char* channel, const char* fmt, ...);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct Impl;
    Impl* impl_;
};

}  // namespace snt::core

// ---------------------------------------------------------------------------
// Public macro API.
//
// Each macro reads the file-local SNT_LOG_CHANNEL define (must be set
// before including log.h). If a translation unit forgets to define it,
// the channel falls back to "unknown" so the call still compiles.
// ---------------------------------------------------------------------------

// Default channel when a .cpp forgets to #define SNT_LOG_CHANNEL.
// Intentionally ugly so the omission is visible in log output.
#ifndef SNT_LOG_CHANNEL
#  define SNT_LOG_CHANNEL "unknown"
#endif

#define SNT_LOG_TRACE(fmt, ...) \
    ::snt::core::Logger::instance().log(::snt::core::LogLevel::kTrace, \
                                        SNT_LOG_CHANNEL, fmt, ##__VA_ARGS__)
#define SNT_LOG_DEBUG(fmt, ...) \
    ::snt::core::Logger::instance().log(::snt::core::LogLevel::kDebug, \
                                        SNT_LOG_CHANNEL, fmt, ##__VA_ARGS__)
#define SNT_LOG_INFO(fmt, ...) \
    ::snt::core::Logger::instance().log(::snt::core::LogLevel::kInfo, \
                                        SNT_LOG_CHANNEL, fmt, ##__VA_ARGS__)
#define SNT_LOG_WARN(fmt, ...) \
    ::snt::core::Logger::instance().log(::snt::core::LogLevel::kWarn, \
                                        SNT_LOG_CHANNEL, fmt, ##__VA_ARGS__)
#define SNT_LOG_ERROR(fmt, ...) \
    ::snt::core::Logger::instance().log(::snt::core::LogLevel::kError, \
                                        SNT_LOG_CHANNEL, fmt, ##__VA_ARGS__)
#define SNT_LOG_FATAL(fmt, ...) \
    ::snt::core::Logger::instance().log(::snt::core::LogLevel::kFatal, \
                                        SNT_LOG_CHANNEL, fmt, ##__VA_ARGS__)
