// ChuckStation5 – Debugger (Phase 8 polished implementation)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "ChuckStation5/Debugger/Debugger.h"

#include "ChuckStation5/Cpu/Cpu.h"
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/ModuleRegistry/ModuleRegistry.h"
#include "ChuckStation5/RuntimeEvents/RuntimeEvents.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on
#pragma comment(lib, "dbghelp.lib")
#ifdef ClearEventLog
#undef ClearEventLog
#endif
#ifdef CreateMutex
#undef CreateMutex
#endif
#ifdef CreateSemaphore
#undef CreateSemaphore
#endif
#ifdef CreateEvent
#undef CreateEvent
#endif
#endif

namespace ChuckStation5::Debugger
{

using Clock = std::chrono::steady_clock;

namespace
{

static uint64_t NowUs()
{
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
}

struct State
{
	bool initialised = false;
	std::mutex mtx;

	// Symbol table
	std::map<uint64_t, std::string> symbols; // addr → name

	// Breakpoints
	std::vector<BreakpointEntry> breakpoints;
	uint32_t nextBpId = 1;

	// Timeline
	static constexpr size_t kMaxTimeline = 1024;
	std::vector<TimelineEvent> timeline;

	// Event log
	std::vector<EventLogEntry> eventLog;
	bool browserAttached = false;

	// Legacy fields
	std::vector<WatchExpression> watches;
	uint32_t nextWatchId = 1;
	BreakpointHitFn bpHitCb;
	StepFn stepCb;
	bool paused = false;
	std::optional<CpuState> cpuState;
	uint32_t nextCondBpId = 10000;
	std::vector<DebugEvent> eventHistory;

	static State& Get()
	{
		static State s;
		return s;
	}
};

} // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────

bool Init()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.symbols.clear();
	st.breakpoints.clear();
	st.nextBpId = 1;
	st.timeline.clear();
	st.eventLog.clear();
	st.browserAttached = false;
	st.watches.clear();
	st.nextWatchId = 1;
	st.bpHitCb = nullptr;
	st.stepCb = nullptr;
	st.paused = false;
	st.cpuState = std::nullopt;
	st.nextCondBpId = 10000;
	st.initialised = true;
	CHUCKSTATION5_INFO("[Debugger] Initialised (Phase 8).");
	return true;
}

void Shutdown()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.initialised = false;
	st.browserAttached = false;
	CHUCKSTATION5_INFO("[Debugger] Shut down.");
}

// ── Register viewer ───────────────────────────────────────────────────────

std::vector<RegEntry> GetRegisterView()
{
	auto& ctx = Cpu::GetContextConst();
	std::vector<RegEntry> out;
	out.reserve(16);
	for (int i = 0; i < 16; ++i)
	{
		RegEntry e;
		e.name = Cpu::RegName(static_cast<Cpu::Reg>(i));
		e.value = ctx.gpr[i];
		out.push_back(e);
	}
	return out;
}

std::vector<SpecialReg> GetSpecialRegisters()
{
	auto& ctx = Cpu::GetContextConst();
	return {
		{"rip", ctx.rip},
		{"rflags", ctx.rflags},
		{"cs", ctx.cs},
		{"ss", ctx.ss},
	};
}

std::vector<FlagEntry> GetFlagsView()
{
	auto& ctx = Cpu::GetContextConst();
	return {
		{"CF", ctx.flag(Cpu::Flags::CF)}, {"PF", ctx.flag(Cpu::Flags::PF)}, {"AF", ctx.flag(Cpu::Flags::AF)},
		{"ZF", ctx.flag(Cpu::Flags::ZF)}, {"SF", ctx.flag(Cpu::Flags::SF)}, {"TF", ctx.flag(Cpu::Flags::TF)},
		{"IF", ctx.flag(Cpu::Flags::IF)}, {"DF", ctx.flag(Cpu::Flags::DF)}, {"OF", ctx.flag(Cpu::Flags::OF)},
	};
}

// ── Call stack ────────────────────────────────────────────────────────────

std::vector<CallFrame> GetCallStack(uint32_t maxDepth)
{
	auto cpu_frames = Cpu::GetCallStack(maxDepth);
	std::vector<CallFrame> out;
	out.reserve(cpu_frames.size());
	for (auto& f : cpu_frames)
	{
		CallFrame cf;
		cf.returnAddr = f.returnAddr;
		cf.frameBase = f.frameBase;
		cf.symbol = f.symbol;
		if (cf.symbol.empty())
		{
			auto sym = LookupSymbol(f.returnAddr);
			if (sym)
				cf.symbol = *sym;
		}
		out.push_back(cf);
	}
	return out;
}

// ── Memory viewer ─────────────────────────────────────────────────────────

std::vector<uint8_t> ReadMemory(uint64_t address, size_t length)
{
	std::vector<uint8_t> out(length);
	std::memcpy(out.data(), reinterpret_cast<const void*>(address), length);
	return out;
}

void WriteMemory(uint64_t address, const std::vector<uint8_t>& data)
{
	std::memcpy(reinterpret_cast<void*>(address), data.data(), data.size());
}

std::string HexDump(uint64_t address, size_t length)
{
	std::ostringstream ss;
	const uint8_t* ptr = reinterpret_cast<const uint8_t*>(address);
	constexpr size_t ROW = 16;
	for (size_t i = 0; i < length; i += ROW)
	{
		ss << std::hex << std::setw(16) << std::setfill('0') << (address + i) << "  ";
		for (size_t j = 0; j < ROW; ++j)
		{
			if (i + j < length)
				ss << std::setw(2) << static_cast<unsigned>(ptr[i + j]) << " ";
			else
				ss << "   ";
			if (j == 7)
				ss << " ";
		}
		ss << " |";
		for (size_t j = 0; j < ROW && i + j < length; ++j)
		{
			char c = static_cast<char>(ptr[i + j]);
			ss << (c >= 0x20 && c < 0x7F ? c : '.');
		}
		ss << "|\n";
	}
	return ss.str();
}

// ── Module browser ────────────────────────────────────────────────────────

std::vector<ModuleEntry> GetModuleList()
{
	auto mods = ModuleRegistry::GetAll();
	std::vector<ModuleEntry> out;
	out.reserve(mods.size());
	for (auto& m : mods)
	{
		ModuleEntry e;
		e.name = m.name;
		e.baseAddr = m.baseAddr;
		e.size = m.size;
		e.id = m.id;
		out.push_back(e);
	}
	return out;
}

// ── Symbol browser ────────────────────────────────────────────────────────

void AddSymbol(uint64_t addr, const std::string& name)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.symbols[addr] = name;
}

void RemoveSymbol(uint64_t addr)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.symbols.erase(addr);
}

std::optional<std::string> LookupSymbol(uint64_t addr)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	auto it = st.symbols.find(addr);
	if (it != st.symbols.end())
		return it->second;
	return std::nullopt;
}

std::optional<NearestSymbolResult> NearestSymbol(uint64_t addr)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	if (st.symbols.empty())
		return std::nullopt;

	// Find the largest key ≤ addr
	auto it = st.symbols.upper_bound(addr);
	if (it == st.symbols.begin())
		return std::nullopt;
	--it;
	NearestSymbolResult r;
	r.addr = it->first;
	r.name = it->second;
	r.offset = addr - it->first;
	return r;
}

// ── Breakpoint manager ────────────────────────────────────────────────────

uint32_t SetBreakpoint(uint64_t addr, const std::string& label)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	uint32_t id = st.nextBpId++;
	BreakpointEntry e;
	e.id = id;
	e.addr = addr;
	e.label = label;
	e.hitCount = 0;
	e.enabled = true;
	st.breakpoints.push_back(e);
	Cpu::AddBreakpoint(addr, label);
	return id;
}

bool RemoveBreakpoint(uint32_t id)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	auto it = std::find_if(st.breakpoints.begin(), st.breakpoints.end(),
						   [id](const BreakpointEntry& b) { return b.id == id; });
	if (it == st.breakpoints.end())
		return false;
	Cpu::RemoveBreakpoint(id);
	st.breakpoints.erase(it);
	return true;
}

void ClearAllBreakpoints()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.breakpoints.clear();
	Cpu::ClearBreakpoints();
}

std::vector<BreakpointEntry> ListBreakpoints()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	return st.breakpoints;
}

void RecordBreakpointHit(uint32_t id)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	for (auto& bp : st.breakpoints)
	{
		if (bp.id == id)
		{
			++bp.hitCount;
			break;
		}
	}
}

// ── Timeline ─────────────────────────────────────────────────────────────

void RecordTimelineEvent(const std::string& category, uint64_t address, uint64_t timestampNs)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	if (st.timeline.size() >= State::kMaxTimeline)
	{
		st.timeline.erase(st.timeline.begin()); // ring-buffer eviction
	}
	TimelineEvent e;
	e.category = category;
	e.address = address;
	e.timestampNs = timestampNs;
	st.timeline.push_back(e);
}

std::vector<TimelineEvent> GetTimeline()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	return st.timeline;
}

void ClearTimeline()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.timeline.clear();
}

size_t MaxTimelineEvents()
{
	return State::kMaxTimeline;
}

// ── Event browser ─────────────────────────────────────────────────────────

void AttachEventBrowser()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	if (st.browserAttached)
		return;
	st.browserAttached = true;

	RuntimeEvents::Subscribe(
		[](const RuntimeEvents::RuntimeEvent& ev)
		{
			auto& s = State::Get();
			std::lock_guard lk2(s.mtx);
			s.eventLog.push_back({ev.type, ev.timestampUs * 1000});
		});
}

std::vector<EventLogEntry> GetEventLog()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	return st.eventLog;
}

std::vector<EventLogEntry> GetEventLog(RuntimeEvents::EventType filter)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	std::vector<EventLogEntry> out;
	for (auto& e : st.eventLog)
	{
		if (e.type == filter)
			out.push_back(e);
	}
	return out;
}

void ClearEventLog()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.eventLog.clear();
}

// ── Legacy API ────────────────────────────────────────────────────────────

uint32_t AddBreakpoint(uint64_t address, std::string label)
{
	return SetBreakpoint(address, label);
}

bool EnableBreakpoint(uint32_t id, bool enable)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	for (auto& bp : st.breakpoints)
	{
		if (bp.id == id)
		{
			bp.enabled = enable;
			return true;
		}
	}
	return false;
}

void ClearBreakpoints()
{
	ClearAllBreakpoints();
}

uint32_t AddWatchpoint(uint64_t address, size_t size, bool, bool, std::string label)
{
	uint32_t id = AddWatch(label, address, size);
	return id > 0 ? id - 1 : 0;
}

void ClearWatchpoints()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.watches.clear();
}

void Continue()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.paused = false;
	st.cpuState = std::nullopt;
	Cpu::Resume();
}
void StepInto()
{
	Cpu::Step();
}
void StepOver()
{
	Cpu::Step();
}

void Pause()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.paused = true;
	Cpu::Pause();
}

bool IsPaused()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	return st.paused;
}

std::optional<CpuState> GetCpuState()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	if (st.cpuState.has_value())
	{
		return st.cpuState;
	}
	if (st.paused)
	{
		auto& ctx = Cpu::GetContextConst();
		CpuState s;
		s.rip = ctx.rip;
		s.rflags = ctx.rflags;
		s.rax = ctx.gpr[0];
		s.rcx = ctx.gpr[1];
		s.rdx = ctx.gpr[2];
		s.rbx = ctx.gpr[3];
		s.rsp = ctx.gpr[4];
		s.rbp = ctx.gpr[5];
		s.rsi = ctx.gpr[6];
		s.rdi = ctx.gpr[7];
		s.r8 = ctx.gpr[8];
		s.r9 = ctx.gpr[9];
		s.r10 = ctx.gpr[10];
		s.r11 = ctx.gpr[11];
		s.r12 = ctx.gpr[12];
		s.r13 = ctx.gpr[13];
		s.r14 = ctx.gpr[14];
		s.r15 = ctx.gpr[15];
		return s;
	}
	return std::nullopt;
}

bool ReadMemory(uint64_t address, void* buf, size_t size)
{
	std::memcpy(buf, reinterpret_cast<const void*>(address), size);
	return true;
}

bool WriteMemory(uint64_t address, const void* buf, size_t size)
{
	std::memcpy(reinterpret_cast<void*>(address), buf, size);
	return true;
}

std::string HexDumpRegion(uint64_t address, size_t length)
{
	return HexDump(address, length);
}

std::vector<StackFrame> GetStackTrace()
{
	auto frames = GetCallStack(32);
	std::vector<StackFrame> out;
	for (auto& f : frames)
	{
		StackFrame sf;
		sf.rip = f.returnAddr;
		sf.label = f.symbol;
		out.push_back(sf);
	}

	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	if (st.cpuState.has_value())
	{
		StackFrame sf;
		sf.rip = st.cpuState->rip;
		sf.rsp = st.cpuState->rsp;
		sf.rbp = st.cpuState->rbp;
		auto it = st.symbols.find(sf.rip);
		if (it != st.symbols.end())
			sf.label = it->second;
		out.push_back(sf);
	}
	return out;
}

bool WriteCrashDump(const std::string& dumpDir)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::create_directories(dumpDir, ec);
	if (ec)
	{
		CHUCKSTATION5_ERROR("[Debugger] WriteCrashDump: cannot create dir '%s': %s", dumpDir.c_str(), ec.message().c_str());
		return false;
	}

	// Build filename: chuckstation5_crash_<timestamp>.dmp (or .txt on non-Windows)
	auto now = std::chrono::system_clock::now().time_since_epoch();
	uint64_t ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
#if defined(_WIN32)
	std::string dumpPath = dumpDir + "/chuckstation5_crash_" + std::to_string(ts) + ".dmp";
#else
	std::string dumpPath = dumpDir + "/chuckstation5_crash_" + std::to_string(ts) + ".txt";
#endif

#if defined(_WIN32)
	// Use Windows MiniDumpWriteDump for a real minidump.
	// DbgHelp.dll is available on all Windows versions.
	HMODULE hDbgHelp = ::LoadLibraryA("dbghelp.dll");
	if (hDbgHelp)
	{
		using MiniDumpWriteDump_t = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
												  PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
		auto pMiniDumpWriteDump =
			reinterpret_cast<MiniDumpWriteDump_t>(::GetProcAddress(hDbgHelp, "MiniDumpWriteDump"));
		if (pMiniDumpWriteDump)
		{
			HANDLE hFile = ::CreateFileA(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
										 FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				BOOL ok = pMiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), hFile,
											 MiniDumpWithDataSegs, nullptr, nullptr, nullptr);
				::CloseHandle(hFile);
				if (ok)
				{
					CHUCKSTATION5_INFO("[Debugger] Crash dump written: '%s'", dumpPath.c_str());
					::FreeLibrary(hDbgHelp);
					return true;
				}
				CHUCKSTATION5_ERROR("[Debugger] MiniDumpWriteDump failed (err=%lu).", ::GetLastError());
			}
		}
		::FreeLibrary(hDbgHelp);
	}
	CHUCKSTATION5_WARN("[Debugger] dbghelp.dll unavailable — writing text crash report.");
#endif

	// Fallback: plain-text crash report with register state + call stack
	std::ofstream f(dumpPath);
	if (!f.is_open())
	{
		CHUCKSTATION5_ERROR("[Debugger] Cannot write crash report to '%s'.", dumpPath.c_str());
		return false;
	}
	f << "ChuckStation5 Crash Report\nTimestamp: " << ts << "\n\n";
	f << "=== Registers ===\n";
	for (auto& r : GetRegisterView())
	{
		f << "  " << r.name << " = 0x" << std::hex << r.value << "\n";
	}
	for (auto& r : GetSpecialRegisters())
	{
		f << "  " << r.name << " = 0x" << std::hex << r.value << "\n";
	}
	f << "\n=== Flags ===\n";
	for (auto& fl : GetFlagsView())
	{
		f << "  " << fl.name << " = " << (fl.set ? "1" : "0") << "\n";
	}
	f << "\n=== Call Stack ===\n";
	auto frames = GetCallStack(32);
	for (size_t i = 0; i < frames.size(); ++i)
	{
		f << "  #" << i << "  0x" << std::hex << frames[i].returnAddr;
		if (!frames[i].symbol.empty())
			f << "  " << frames[i].symbol;
		f << "\n";
	}
	f << "\n=== Timeline (last 20) ===\n";
	auto tl = GetTimeline();
	size_t start = tl.size() > 20 ? tl.size() - 20 : 0;
	for (size_t i = start; i < tl.size(); ++i)
	{
		f << "  [" << tl[i].timestampNs << "] " << tl[i].category << " @ 0x" << std::hex << tl[i].address << "\n";
	}
	CHUCKSTATION5_INFO("[Debugger] Text crash report written: '%s'", dumpPath.c_str());
	return true;
}

void RegisterBreakpointCallback(BreakpointHitFn fn)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.bpHitCb = std::move(fn);
}

void RegisterStepCallback(StepFn fn)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.stepCb = std::move(fn);
}

void OnBreakpointHit(uint32_t id, const CpuState& state)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.paused = true;
	st.cpuState = state;
	for (auto& bp : st.breakpoints)
	{
		if (bp.id == id)
		{
			++bp.hitCount;
			if (st.bpHitCb)
			{
				Breakpoint legacy;
				legacy.address = bp.addr;
				legacy.enabled = bp.enabled;
				legacy.label = bp.label;
				legacy.hitCount = bp.hitCount;
				st.bpHitCb(legacy, state);
			}
			break;
		}
	}
}

uint32_t AddConditionalBreakpoint(uint64_t address, BreakConditionFn, std::string label)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	uint32_t id = st.nextCondBpId++;
	BreakpointEntry e;
	e.id = id;
	e.addr = address;
	e.label = label;
	e.hitCount = 0;
	e.enabled = true;
	st.breakpoints.push_back(e);
	Cpu::AddBreakpoint(address, label);

	DebugEvent de;
	de.timestampUs = NowUs();
	de.type = "ConditionalBreakpoint";
	de.description = label;
	de.address = address;
	st.eventHistory.push_back(de);

	return id;
}

uint32_t AddWatch(std::string name, uint64_t address, size_t size)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	WatchExpression w;
	w.id = st.nextWatchId++;
	w.name = std::move(name);
	w.address = address;
	w.size = (size > 8) ? 8 : size;
	st.watches.push_back(w);
	return w.id;
}

bool RemoveWatch(uint32_t id)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	auto it = std::find_if(st.watches.begin(), st.watches.end(), [id](const WatchExpression& w) { return w.id == id; });
	if (it == st.watches.end())
		return false;
	st.watches.erase(it);
	return true;
}

void UpdateWatches()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	for (auto& w : st.watches)
	{
		uint64_t val = 0;
		size_t readSize = std::min(w.size, sizeof(val));
		if (Memory::IsReadable(w.address, readSize))
		{
			std::memcpy(&val, reinterpret_cast<const void*>(w.address), readSize);
		}
		w.changed = (val != w.lastValue);
		w.lastValue = val;
	}
}

std::vector<WatchExpression> GetWatches()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	return st.watches;
}

std::vector<SymbolEntry> BrowseSymbols(const std::string& filter)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	std::vector<SymbolEntry> out;
	for (auto& [addr, name] : st.symbols)
	{
		if (!filter.empty() && name.find(filter) == std::string::npos)
			continue;
		SymbolEntry e;
		e.name = name;
		e.address = addr;
		out.push_back(e);
	}
	return out;
}

std::string AddressToSymbol(uint64_t address)
{
	auto sym = LookupSymbol(address);
	if (sym)
		return *sym;
	auto nearSym = NearestSymbol(address);
	if (nearSym)
		return nearSym->name + "+0x" + std::to_string(nearSym->offset);
	return "";
}

std::vector<DebugEvent> GetEventHistory(size_t maxEvents)
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	if (!st.eventHistory.empty())
	{
		size_t n = std::min(st.eventHistory.size(), maxEvents);
		return std::vector<DebugEvent>(st.eventHistory.end() - static_cast<ptrdiff_t>(n), st.eventHistory.end());
	}
	std::vector<DebugEvent> out;
	size_t n = std::min(st.eventLog.size(), maxEvents);
	auto begin = st.eventLog.end() - static_cast<ptrdiff_t>(n);
	for (auto it = begin; it != st.eventLog.end(); ++it)
	{
		DebugEvent e;
		e.timestampUs = it->timestampNs / 1000;
		e.type = "event";
		out.push_back(e);
	}
	return out;
}

void ClearEventHistory()
{
	auto& st = State::Get();
	std::lock_guard lk(st.mtx);
	st.eventHistory.clear();
	st.eventLog.clear();
}

} // namespace ChuckStation5::Debugger
