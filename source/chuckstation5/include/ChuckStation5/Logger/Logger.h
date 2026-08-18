// ChuckStation5 – Logger module
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
#pragma once

#include <cstdint>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>

namespace ChuckStation5::Logger {

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

} // namespace ChuckStation5::Logger

// ── Convenience macros ────────────────────────────────────────────────────
#define CHUCKSTATION5_LOG_TAG "ChuckStation5"

#define CHUCKSTATION5_LOG(lvl, tag, fmt, ...)                                          \
    do {                                                                       \
        if (ChuckStation5::Logger::IsEnabled(lvl)) {                                   \
            ChuckStation5::Logger::WriteF(lvl, tag, std::source_location::current(),   \
                                 fmt, ##__VA_ARGS__);                          \
        }                                                                      \
    } while (false)

#define CHUCKSTATION5_TRACE(fmt, ...)   CHUCKSTATION5_LOG(ChuckStation5::Logger::Level::Trace,   CHUCKSTATION5_LOG_TAG, fmt, ##__VA_ARGS__)
#define CHUCKSTATION5_DEBUG(fmt, ...)   CHUCKSTATION5_LOG(ChuckStation5::Logger::Level::Debug,   CHUCKSTATION5_LOG_TAG, fmt, ##__VA_ARGS__)
#define CHUCKSTATION5_INFO(fmt, ...)    CHUCKSTATION5_LOG(ChuckStation5::Logger::Level::Info,    CHUCKSTATION5_LOG_TAG, fmt, ##__VA_ARGS__)
#define CHUCKSTATION5_WARN(fmt, ...)    CHUCKSTATION5_LOG(ChuckStation5::Logger::Level::Warning, CHUCKSTATION5_LOG_TAG, fmt, ##__VA_ARGS__)
#define CHUCKSTATION5_ERROR(fmt, ...)   CHUCKSTATION5_LOG(ChuckStation5::Logger::Level::Error,   CHUCKSTATION5_LOG_TAG, fmt, ##__VA_ARGS__)
#define CHUCKSTATION5_FATAL(fmt, ...)   CHUCKSTATION5_LOG(ChuckStation5::Logger::Level::Fatal,   CHUCKSTATION5_LOG_TAG, fmt, ##__VA_ARGS__)
