// PS5x – Logger module
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
#pragma once

#include <cstdint>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>

namespace PS5x::Logger {

/// Severity levels (ordered lowest → highest).
enum class Level : uint8_t
{
    Trace   = 0,
    Debug   = 1,
    Info    = 2,
    Warning = 3,
    Error   = 4,
    Fatal   = 5,
    Off     = 255,
};

/// Callback type for external log sinks (e.g. UI console pane).
using SinkFn = std::function<void(Level, std::string_view /*tag*/, std::string_view /*msg*/)>;

// ── Lifecycle ────────────────────────────────────────────────────────────────

/// Initialize the logger.  Call once at startup before any log calls.
/// @param logPath   UTF-8 path for the rotating log file.  Empty = no file.
/// @param console   Whether to echo to stdout/stderr.
/// @param minLevel  Minimum level that passes the filter.
void Init(std::string_view logPath = "", bool console = true, Level minLevel = Level::Trace);

/// Shut down all sinks and flush buffers.
void Shutdown();

// ── Sinks ─────────────────────────────────────────────────────────────────

/// Register an additional (e.g. UI) sink.  Thread-safe.
void AddSink(SinkFn sink);

/// Remove all user-registered sinks.
void ClearSinks();

// ── Core log function ─────────────────────────────────────────────────────

/// Low-level write – prefer the macros below.
void Write(Level level, std::string_view tag, std::string_view message,
           const std::source_location& loc = std::source_location::current());

/// printf-style variant.
void WriteF(Level level, std::string_view tag,
            const std::source_location& loc, const char* fmt, ...)
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

// ── Runtime control ──────────────────────────────────────────────────────

void SetLevel(Level level);
Level GetLevel();
bool IsEnabled(Level level);

} // namespace PS5x::Logger

// ── Convenience macros ────────────────────────────────────────────────────
#define PS5X_LOG_TAG "PS5x"

#define PS5X_LOG(lvl, tag, fmt, ...)                                          \
    do {                                                                       \
        if (PS5x::Logger::IsEnabled(lvl)) {                                   \
            PS5x::Logger::WriteF(lvl, tag, std::source_location::current(),   \
                                 fmt, ##__VA_ARGS__);                          \
        }                                                                      \
    } while (false)

#define PS5X_TRACE(fmt, ...)   PS5X_LOG(PS5x::Logger::Level::Trace,   PS5X_LOG_TAG, fmt, ##__VA_ARGS__)
#define PS5X_DEBUG(fmt, ...)   PS5X_LOG(PS5x::Logger::Level::Debug,   PS5X_LOG_TAG, fmt, ##__VA_ARGS__)
#define PS5X_INFO(fmt, ...)    PS5X_LOG(PS5x::Logger::Level::Info,    PS5X_LOG_TAG, fmt, ##__VA_ARGS__)
#define PS5X_WARN(fmt, ...)    PS5X_LOG(PS5x::Logger::Level::Warning, PS5X_LOG_TAG, fmt, ##__VA_ARGS__)
#define PS5X_ERROR(fmt, ...)   PS5X_LOG(PS5x::Logger::Level::Error,   PS5X_LOG_TAG, fmt, ##__VA_ARGS__)
#define PS5X_FATAL(fmt, ...)   PS5X_LOG(PS5x::Logger::Level::Fatal,   PS5X_LOG_TAG, fmt, ##__VA_ARGS__)
