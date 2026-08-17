// PS5x – Runtime Manager implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/Runtime/Runtime.h"

#include "PS5x/Logger/Logger.h"

// Forward-declare subsystem init/shutdown so we can build the default table
// without pulling headers into the runtime itself (keeps deps acyclic).
#include "PS5x/Audio/Audio.h"
#include "PS5x/Config/Config.h"
#include "PS5x/Debugger/Debugger.h"
#include "PS5x/Filesystem/Filesystem.h"
#include "PS5x/GPU/GPU.h"
#include "PS5x/Input/Input.h"
#include "PS5x/Kernel/Kernel.h"
#include "PS5x/KytyAdapter/KytyAdapter.h"
#include "PS5x/Loader/Loader.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/Renderer/RendererBackend.h"
#include "PS5x/UI/UI.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace PS5x::Runtime
{

// ── Name tables ───────────────────────────────────────────────────────────
const char* SubsystemName(SubsystemId id)
{
	static constexpr std::array<const char*, 14> kNames = {"Logger", "Config",      "Memory",   "Kernel", "Filesystem",
														   "Loader", "KytyAdapter", "Renderer", "GPU",    "Audio",
														   "Input",  "Process",     "Debugger", "UI"};
	auto i = static_cast<size_t>(id);
	return i < kNames.size() ? kNames[i] : "Unknown";
}

const char* SubsystemStateName(SubsystemState s)
{
	switch (s)
	{
	case SubsystemState::Unregistered:
		return "Unregistered";
	case SubsystemState::Registered:
		return "Registered";
	case SubsystemState::Initialising:
		return "Initialising";
	case SubsystemState::Running:
		return "Running";
	case SubsystemState::ShuttingDown:
		return "ShuttingDown";
	case SubsystemState::Stopped:
		return "Stopped";
	case SubsystemState::Failed:
		return "Failed";
	}
	return "?";
}

// ── Runtime state ─────────────────────────────────────────────────────────
namespace
{

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;

struct Entry
{
	SubsystemDesc desc;
	SubsystemTiming timing;
};

struct RuntimeState
{
	std::unordered_map<uint32_t, Entry> entries;
	std::vector<SubsystemId> initOrder; // topological order used
	std::mutex mtx;

	static RuntimeState& Get()
	{
		static RuntimeState s;
		return s;
	}
};

double ElapsedMs(Clock::time_point t0)
{
	return std::chrono::duration_cast<Ms>(Clock::now() - t0).count();
}

// Topological sort (Kahn's algorithm)
std::vector<SubsystemId> TopoSort(const RuntimeState& st)
{
	std::unordered_map<uint32_t, int> inDegree;
	std::unordered_map<uint32_t, std::vector<uint32_t>> adj;

	for (auto& [k, e] : st.entries)
	{
		if (!inDegree.count(k))
			inDegree[k] = 0;
		for (auto dep : e.desc.deps)
		{
			uint32_t dk = static_cast<uint32_t>(dep);
			adj[dk].push_back(k);
			inDegree[k]++;
		}
	}

	std::vector<SubsystemId> order;
	std::vector<uint32_t> q;
	for (auto& [k, deg] : inDegree)
		if (deg == 0)
			q.push_back(k);

	while (!q.empty())
	{
		auto cur = q.back();
		q.pop_back();
		order.push_back(static_cast<SubsystemId>(cur));
		for (auto nxt : adj[cur])
		{
			if (--inDegree[nxt] == 0)
				q.push_back(nxt);
		}
	}
	return order;
}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────

void Register(SubsystemDesc desc)
{
	auto& st = RuntimeState::Get();
	std::lock_guard lk(st.mtx);
	uint32_t k = static_cast<uint32_t>(desc.id);
	Entry e;
	e.desc = std::move(desc);
	e.timing.id = e.desc.id;
	e.timing.state = SubsystemState::Registered;
	st.entries[k] = std::move(e);
}

bool InitAll()
{
	// Snapshot the sorted order with lock held, then release before calling init fns
	// to avoid deadlock if init callbacks re-enter the Runtime.
	std::vector<SubsystemId> order;
	{
		auto& st = RuntimeState::Get();
		std::lock_guard lk(st.mtx);
		order = TopoSort(st);
	}

	bool ok = true;
	for (auto id : order)
	{
		auto& st = RuntimeState::Get();

		// Check deps (lock)
		bool depsOk = true;
		bool isOptional = false;
		{
			std::lock_guard lk(st.mtx);
			auto it = st.entries.find(static_cast<uint32_t>(id));
			if (it == st.entries.end())
				continue;
			isOptional = it->second.desc.optional;
			for (auto dep : it->second.desc.deps)
			{
				uint32_t dk = static_cast<uint32_t>(dep);
				auto dit = st.entries.find(dk);
				if (dit == st.entries.end() || dit->second.timing.state != SubsystemState::Running)
				{
					depsOk = false;
					break;
				}
			}
			if (!depsOk && !isOptional)
			{
				it->second.timing.state = SubsystemState::Failed;
				it->second.timing.failReason = "dependency not running";
			}
			else
			{
				it->second.timing.state = SubsystemState::Initialising;
			}
		}
		if (!depsOk && !isOptional)
		{
			ok = false;
			continue;
		}
		if (!depsOk)
			continue;

		// Call init WITHOUT lock
		InitFn initFn;
		{
			std::lock_guard lk(st.mtx);
			auto it = st.entries.find(static_cast<uint32_t>(id));
			if (it == st.entries.end())
				continue;
			initFn = it->second.desc.init;
		}

		auto t0 = Clock::now();
		bool initOk = initFn ? initFn() : true;
		double elapsed = ElapsedMs(t0);

		// Record result (lock)
		{
			std::lock_guard lk(st.mtx);
			auto it = st.entries.find(static_cast<uint32_t>(id));
			if (it == st.entries.end())
				continue;
			auto& e = it->second;
			e.timing.initMs = elapsed;
			if (initOk)
			{
				e.timing.state = SubsystemState::Running;
				st.initOrder.push_back(id);
			}
			else
			{
				e.timing.state = SubsystemState::Failed;
				e.timing.failReason = "init() returned false";
				if (!isOptional)
					ok = false;
			}
		}
	}

	ReportTimings();
	return ok;
}

void ShutdownAll()
{
	auto& st = RuntimeState::Get();
	std::lock_guard lk(st.mtx);

	// Reverse of init order
	auto order = st.initOrder;
	std::reverse(order.begin(), order.end());

	for (auto id : order)
	{
		uint32_t k = static_cast<uint32_t>(id);
		auto it = st.entries.find(k);
		if (it == st.entries.end())
			continue;
		auto& e = it->second;
		if (e.timing.state != SubsystemState::Running)
			continue;

		e.timing.state = SubsystemState::ShuttingDown;
		auto t0 = Clock::now();
		if (e.desc.shutdown)
			e.desc.shutdown();
		e.timing.shutdownMs = ElapsedMs(t0);
		e.timing.state = SubsystemState::Stopped;
		PS5X_INFO("[Runtime]  ↓ %-14s  %.2f ms", SubsystemName(id), e.timing.shutdownMs);
	}

	st.initOrder.clear();
}

void Reset()
{
	auto& st = RuntimeState::Get();
	std::lock_guard lk(st.mtx);
	st.entries.clear();
	st.initOrder.clear();
}

bool InitOne(SubsystemId id)
{
	auto& st = RuntimeState::Get();
	std::lock_guard lk(st.mtx);
	uint32_t k = static_cast<uint32_t>(id);
	auto it = st.entries.find(k);
	if (it == st.entries.end())
		return false;
	auto& e = it->second;
	if (e.timing.state == SubsystemState::Running)
		return true;

	e.timing.state = SubsystemState::Initialising;
	auto t0 = Clock::now();
	bool ok = e.desc.init ? e.desc.init() : true;
	e.timing.initMs = ElapsedMs(t0);
	e.timing.state = ok ? SubsystemState::Running : SubsystemState::Failed;
	if (ok)
		st.initOrder.push_back(id);
	return ok;
}

void ShutdownOne(SubsystemId id)
{
	auto& st = RuntimeState::Get();
	std::lock_guard lk(st.mtx);
	uint32_t k = static_cast<uint32_t>(id);
	auto it = st.entries.find(k);
	if (it == st.entries.end())
		return;
	auto& e = it->second;
	if (e.timing.state != SubsystemState::Running)
		return;
	e.timing.state = SubsystemState::ShuttingDown;
	auto t0 = Clock::now();
	if (e.desc.shutdown)
		e.desc.shutdown();
	e.timing.shutdownMs = ElapsedMs(t0);
	e.timing.state = SubsystemState::Stopped;
}

SubsystemState GetState(SubsystemId id)
{
	auto& st = RuntimeState::Get();
	std::lock_guard lk(st.mtx);
	uint32_t k = static_cast<uint32_t>(id);
	auto it = st.entries.find(k);
	if (it == st.entries.end())
		return SubsystemState::Unregistered;
	return it->second.timing.state;
}

bool IsRunning(SubsystemId id)
{
	return GetState(id) == SubsystemState::Running;
}

std::optional<SubsystemTiming> GetTiming(SubsystemId id)
{
	auto& st = RuntimeState::Get();
	std::lock_guard lk(st.mtx);
	uint32_t k = static_cast<uint32_t>(id);
	auto it = st.entries.find(k);
	if (it == st.entries.end())
		return std::nullopt;
	return it->second.timing;
}

std::vector<SubsystemTiming> GetAllTimings()
{
	auto& st = RuntimeState::Get();
	std::lock_guard lk(st.mtx);
	std::vector<SubsystemTiming> out;
	for (auto& [k, e] : st.entries)
		out.push_back(e.timing);
	std::sort(out.begin(), out.end(), [](const SubsystemTiming& a, const SubsystemTiming& b)
			  { return static_cast<uint32_t>(a.id) < static_cast<uint32_t>(b.id); });
	return out;
}

void ReportTimings()
{
	auto timings = GetAllTimings();
	PS5X_INFO("[Runtime] ── Subsystem Timing Report ──────────────────");
	double total = 0.0;
	for (const auto& t : timings)
	{
		if (t.state == SubsystemState::Unregistered)
			continue;
		PS5X_INFO("[Runtime]  %-14s  %s  init=%.2f ms  shutdown=%.2f ms%s", SubsystemName(t.id),
				  SubsystemStateName(t.state), t.initMs, t.shutdownMs,
				  t.failReason.empty() ? "" : (" [" + t.failReason + "]").c_str());
		total += t.initMs;
	}
	PS5X_INFO("[Runtime]  Total init time: %.2f ms", total);
	PS5X_INFO("[Runtime] ────────────────────────────────────────────");
}

// ── Default subsystem table ───────────────────────────────────────────────

void RegisterDefaults()
{
	using Id = SubsystemId;

	// Logger – no deps, always first
	Register({Id::Logger,
			  "Logger",
			  []
			  {
				  PS5x::Logger::Init("ps5x.log", true, PS5x::Logger::Level::Info);
				  return true;
			  },
			  [] { PS5x::Logger::Shutdown(); },
			  {},
			  false});

	// Config
	Register({Id::Config,
			  "Config",
			  []
			  {
				  PS5x::Config::Reset();
				  return true;
			  },
			  [] {},
			  {Id::Logger},
			  false});

	// Memory
	Register({Id::Memory,
			  "Memory",
			  [] { return PS5x::Memory::Init(); },
			  [] { PS5x::Memory::Shutdown(); },
			  {Id::Logger},
			  false});

	// Kernel
	Register({Id::Kernel,
			  "Kernel",
			  []
			  {
				  PS5x::Kernel::Init();
				  return true;
			  },
			  [] { PS5x::Kernel::Shutdown(); },
			  {Id::Memory},
			  false});

	// Filesystem
	Register({Id::Filesystem,
			  "Filesystem",
			  []
			  {
				  PS5x::Filesystem::Init();
				  return true;
			  },
			  [] { PS5x::Filesystem::Shutdown(); },
			  {Id::Logger, Id::Config},
			  false});

	// Loader
	Register({Id::Loader,
			  "Loader",
			  []
			  {
				  PS5x::Loader::Init();
				  return true;
			  },
			  [] { PS5x::Loader::Shutdown(); },
			  {Id::Memory, Id::Filesystem},
			  false});

	// KytyAdapter
	Register({Id::KytyAdapter,
			  "KytyAdapter",
			  [] { return PS5x::KytyAdapter::Init(); },
			  [] { PS5x::KytyAdapter::Shutdown(); },
			  {Id::Memory},
			  false});

	// Renderer (optional – no display in CI)
	Register({Id::Renderer,
			  "Renderer",
			  []
			  {
				  return true; /* backend created on demand */
			  },
			  [] {},
			  {Id::Config},
			  true});

	// GPU
	Register({Id::GPU,
			  "GPU",
			  []
			  {
				  return true; /* Init(backend) called after window creation */
			  },
			  [] { PS5x::GPU::Shutdown(); },
			  {Id::Renderer},
			  true});

	// Audio
	Register({
		Id::Audio,
		"Audio",
		[]
		{
			const auto& cfg = PS5x::Config::Get();
			PS5x::Audio::AudioConfig ac;
			ac.sampleRate = cfg.audio.sampleRate;
			ac.bufferSamples = cfg.audio.bufferSize;
			ac.masterVolume = cfg.audio.masterVolume;
			return PS5x::Audio::Init(ac); // may return false in CI – optional
		},
		[] { PS5x::Audio::Shutdown(); },
		{Id::Config},
		true // optional
	});

	// Input
	Register({Id::Input,
			  "Input",
			  []
			  {
				  PS5x::Input::Init();
				  return true;
			  },
			  [] { PS5x::Input::Shutdown(); },
			  {Id::Config},
			  false});

	// Debugger
	Register({Id::Debugger,
			  "Debugger",
			  []
			  {
				  if (PS5x::Config::Get().debug.enableDebugger)
					  PS5x::Debugger::Init();
				  return true;
			  },
			  []
			  {
				  if (PS5x::Config::Get().debug.enableDebugger)
					  PS5x::Debugger::Shutdown();
			  },
			  {Id::Memory, Id::Logger},
			  false});

	// UI (optional in headless mode)
	Register({Id::UI,
			  "UI",
			  []
			  {
				  return true; /* UI::Init() called with window params */
			  },
			  [] { PS5x::UI::Shutdown(); },
			  {Id::Logger, Id::Config},
			  true});
}

} // namespace PS5x::Runtime
