// PS5x – Logger implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/Logger/Logger.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>

namespace PS5x::Logger
{

namespace
{

struct State
{
	std::atomic<Level> minLevel{Level::Trace};
	std::atomic<bool> consoleEnabled{true};
	std::ofstream fileStream;
	std::mutex sinkMutex;
	std::vector<SinkFn> sinks;

	static State& Get()
	{
		static State s;
		return s;
	}
};

constexpr std::array<const char*, 6> kLevelName{"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};

std::string FormatTimestamp()
{
	using namespace std::chrono;
	const auto now = system_clock::now();
	const auto time = system_clock::to_time_t(now);
	const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

	char buf[32];
	std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&time));
	char result[48];
	std::snprintf(result, sizeof(result), "%s.%03lld", buf, static_cast<long long>(ms.count()));
	return result;
}

void Dispatch(Level level, std::string_view tag, std::string_view message, const std::source_location& loc)
{
	auto& st = State::Get();

	if (static_cast<uint8_t>(level) < static_cast<uint8_t>(st.minLevel.load(std::memory_order_relaxed)))
		return;

	const auto ts = FormatTimestamp();
	const auto lvlName = kLevelName[static_cast<size_t>(level)];

	char line[2048];
	std::snprintf(line, sizeof(line), "[%s][%s][%.*s] %.*s  (%s:%u)\n", ts.c_str(), lvlName,
				  static_cast<int>(tag.size()), tag.data(), static_cast<int>(message.size()), message.data(),
				  loc.file_name(), loc.line());

	std::lock_guard lock(st.sinkMutex);

	if (st.consoleEnabled.load())
	{
		FILE* out = (level >= Level::Error) ? stderr : stdout;
		std::fputs(line, out);
	}

	if (st.fileStream.is_open())
		st.fileStream << line;

	for (auto& sink : st.sinks)
		sink(level, tag, message);
}

} // anonymous namespace

// ── Public API ───────────────────────────────────────────────────────────────

void Init(std::string_view logPath, bool console, Level minLevel)
{
	auto& st = State::Get();
	st.minLevel.store(minLevel, std::memory_order_relaxed);
	st.consoleEnabled.store(console, std::memory_order_relaxed);

	if (!logPath.empty())
	{
		std::lock_guard lock(st.sinkMutex);
		st.fileStream.open(std::string(logPath), std::ios::out | std::ios::app);
	}

	PS5X_INFO("Logger initialized (level=%u, file=%s)", static_cast<unsigned>(minLevel),
			  logPath.empty() ? "none" : std::string(logPath).c_str());
}

void Shutdown()
{
	auto& st = State::Get();
	{
		std::lock_guard lock(st.sinkMutex);
		if (st.fileStream.is_open())
			st.fileStream.close();
		st.sinks.clear();
	}
}

void AddSink(SinkFn sink)
{
	auto& st = State::Get();
	std::lock_guard lock(st.sinkMutex);
	st.sinks.push_back(std::move(sink));
}

void ClearSinks()
{
	auto& st = State::Get();
	std::lock_guard lock(st.sinkMutex);
	st.sinks.clear();
}

void Write(Level level, std::string_view tag, std::string_view message, const std::source_location& loc)
{
	Dispatch(level, tag, message, loc);
}

void WriteF(Level level, std::string_view tag, const std::source_location& loc, const char* fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	Dispatch(level, tag, buf, loc);
}

void SetLevel(Level level)
{
	State::Get().minLevel.store(level, std::memory_order_relaxed);
}

Level GetLevel()
{
	return State::Get().minLevel.load(std::memory_order_relaxed);
}

bool IsEnabled(Level level)
{
	return static_cast<uint8_t>(level) >= static_cast<uint8_t>(State::Get().minLevel.load(std::memory_order_relaxed));
}

} // namespace PS5x::Logger
