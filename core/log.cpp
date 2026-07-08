// Lightweight logging system — implementation.
//
// Single stderr sink with ANSI color, serialized via mutex. Output format:
//   [HH:MM:SS.mmm][T#12345][LEVEL][channel] message
//
// Color codes:
//   Trace = dark gray     Debug = gray       Info  = default
//   Warn  = yellow        Error = red        Fatal = bold red

#include "core/log.h"

// Define the implementation-side default channel so this TU itself
// can log without triggering the "unknown" fallback in log.h.
#ifndef SNT_LOG_CHANNEL
#  define SNT_LOG_CHANNEL "core"
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace snt::core {

namespace {

// Per-level short labels (fixed 5-char width for alignment).
constexpr const char* kLevelLabels[] = {
    "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL",
};

// ANSI color codes per level. Empty string means "default color".
constexpr const char* kLevelColors[] = {
    "\x1b[90m",  // Trace - dark gray
    "\x1b[37m",  // Debug - light gray
    "",          // Info  - default
    "\x1b[33m",  // Warn  - yellow
    "\x1b[31m",  // Error - red
    "\x1b[1;31m", // Fatal - bold red
};

constexpr const char* kColorReset = "\x1b[0m";

// How often (in writes) to check the file size for rotation. Checking
// every write would add an ftell() call per log line; checking every
// 1024 writes amortizes the cost to near-zero while still rotating
// promptly (at most 1024 lines of overshoot past the size limit).
constexpr int kRotationCheckInterval = 1024;

// Format a wall-clock timestamp as HH:MM:SS.mmm into `buf` (>= 13 bytes).
void format_timestamp(char* buf, size_t buf_size) {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::snprintf(buf, buf_size, "%02d:%02d:%02d.%03d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms.count()));
}

// Get the current size of an open file. Returns 0 on error (treated as
// "no rotation needed" by the caller).
long file_size(std::FILE* f) {
    if (!f) return 0;
    long old = std::ftell(f);
    if (old < 0) return 0;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, old, SEEK_SET);
    return size < 0 ? 0 : size;
}

// Rotate the log file: close it, rename base -> base.1, base.1 -> base.2,
// ..., delete the oldest. Then reopen base in append mode.
// `max_files` is the number of rotated copies to keep (base.1 .. base.N).
// Returns the newly opened FILE*, or nullptr on failure.
std::FILE* rotate_and_reopen(const std::string& base, int max_files) {
    // Walk the rename chain from oldest to newest:
    //   delete base.N
    //   rename base.N-1 -> base.N
    //   ...
    //   rename base.1 -> base.2
    //   rename base   -> base.1
    // Then reopen base fresh.
    for (int i = max_files; i >= 1; --i) {
        std::string cur  = base + "." + std::to_string(i);
        std::string next = base + "." + std::to_string(i + 1);
        if (i == max_files) {
            // The oldest slot: delete whatever's there (may not exist).
            std::remove(next.c_str());
        }
        // Rename cur -> next. If cur doesn't exist, rename fails silently;
        // that's fine (sparse rotation history is acceptable).
        std::rename(cur.c_str(), next.c_str());
    }
    // Finally: base -> base.1
    std::rename(base.c_str(), (base + ".1").c_str());
    // Reopen base in append mode (creates a fresh file).
    return std::fopen(base.c_str(), "a");
}

}  // namespace

// ---------------------------------------------------------------------------
// Logger::Impl: holds mutable state guarded by mutex.
//
// File sink + rotation design:
//   - file_ is a raw FILE* opened by add_file_sink() in append mode.
//   - base_path_ / max_size_ / max_files_ configure rotation; they're
//     captured at add_file_sink() time so rotation can happen later
//     without re-passing them.
//   - write_count_ is checked every kRotationCheckInterval writes; when
//     it hits the threshold, we ftell() the file and rotate if it
//     exceeds max_size_. This amortizes the stat cost.
//   - ANSI color codes are NOT written to the file (only to stderr) so
//     the file is greppable + readable in a text editor.
// ---------------------------------------------------------------------------
struct Logger::Impl {
    std::mutex  mutex;
    LogLevel    min_level = LogLevel::kInfo;
    std::FILE*  file = nullptr;       // optional file sink; nullptr = disabled
    std::string base_path;            // path passed to add_file_sink
    size_t      max_size = 5 * 1024 * 1024;  // rotate when file exceeds this
    int         max_files = 3;        // keep base.1 .. base.N
    int         write_count = 0;      // since last size check
};

Logger::Logger() : impl_(new Impl()) {}

Logger::~Logger() {
    if (impl_->file) {
        std::fclose(impl_->file);
        impl_->file = nullptr;
    }
    delete impl_;
}

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::set_level(LogLevel level) {
    // set_level may be called from any thread; guard the write.
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->min_level = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->min_level;
}

bool Logger::add_file_sink(const char* path, size_t max_size, int max_files) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // Close any previously opened file sink (only one file sink at a time).
    if (impl_->file) {
        std::fclose(impl_->file);
        impl_->file = nullptr;
    }
    impl_->base_path  = path ? path : "";
    impl_->max_size   = max_size;
    impl_->max_files  = max_files;
    impl_->write_count = 0;

    // If the existing file is already over the limit, rotate it now so
    // this run starts with a fresh file (instead of appending to an
    // already-oversized one).
    if (!impl_->base_path.empty()) {
        if (std::FILE* existing = std::fopen(impl_->base_path.c_str(), "r")) {
            long sz = file_size(existing);
            std::fclose(existing);
            if (static_cast<size_t>(sz) >= max_size) {
                impl_->file = rotate_and_reopen(impl_->base_path, impl_->max_files);
            } else {
                // Under the limit: append to the existing file.
                impl_->file = std::fopen(impl_->base_path.c_str(), "a");
            }
        } else {
            // File doesn't exist (or can't be read): create it fresh.
            impl_->file = std::fopen(impl_->base_path.c_str(), "a");
        }
    }
    return impl_->file != nullptr;
}

void Logger::log(LogLevel level, const char* channel, const char* fmt, ...) {
    // Fast path: filter without locking. The level check is a single int
    // read; a torn read here only risks emitting one stray message, which
    // is acceptable. The actual write below is still mutex-guarded.
    if (static_cast<int>(level) < static_cast<int>(impl_->min_level)) {
        return;
    }

    // Format the user message first (outside the lock to minimize hold time).
    char msg_buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    // Build the full log line.
    char ts_buf[16];
    format_timestamp(ts_buf, sizeof(ts_buf));

    const int level_idx = static_cast<int>(level);
    const char* color   = kLevelColors[level_idx];
    const char* label   = kLevelLabels[level_idx];
    const auto  tid     = std::hash<std::thread::id>{}(std::this_thread::get_id());

    // Acquire the lock only for the actual write (atomic w.r.t. other logs).
    std::lock_guard<std::mutex> lock(impl_->mutex);

    // Re-check level inside the lock in case set_level ran between the
    // fast-path check and now.
    if (static_cast<int>(level) < static_cast<int>(impl_->min_level)) {
        return;
    }

    // stderr sink: with ANSI color codes (for interactive terminal use).
    std::fprintf(stderr, "%s[%s][T#%x][%s][%-15s] %s%s\n",
                 color, ts_buf,
                 static_cast<unsigned>(tid),
                 label,
                 channel ? channel : "",
                 msg_buf,
                 kColorReset);

    // File sink (if enabled): plain text, no ANSI codes, so the file is
    // greppable + readable in a text editor. Written in the same locked
    // section so stderr/file ordering stays consistent.
    if (impl_->file) {
        std::fprintf(impl_->file, "[%s][T#%x][%s][%-15s] %s\n",
                     ts_buf,
                     static_cast<unsigned>(tid),
                     label,
                     channel ? channel : "",
                     msg_buf);
        std::fflush(impl_->file);

        // Rotation check: every kRotationCheckInterval writes, see if the
        // file has grown past max_size. If so, close + rename chain +
        // reopen. This bounds the file size across long-running sessions.
        if (++impl_->write_count >= kRotationCheckInterval) {
            impl_->write_count = 0;
            long sz = file_size(impl_->file);
            if (static_cast<size_t>(sz) >= impl_->max_size) {
                std::fclose(impl_->file);
                impl_->file = rotate_and_reopen(impl_->base_path,
                                                 impl_->max_files);
                // If rotation failed to reopen, file_ is nullptr — log
                // continues to stderr only (best-effort, same as a failed
                // initial add_file_sink).
            }
        }
    }
}

}  // namespace snt::core
