// ChuckStation5 – Debugger module
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors

//   • Fully typed register view (GetRegisterView / GetSpecialRegisters / GetFlagsView)
//   • Typed call-stack (GetCallStack)
//   • ReadMemory / WriteMemory returning byte vectors / applying patches
//   • HexDump returning formatted string
//   • GetModuleList (delegates to ModuleRegistry)
//   • Symbol table: AddSymbol / LookupSymbol / NearestSymbol
//   • Breakpoints: SetBreakpoint / RemoveBreakpoint / ClearAllBreakpoints /
//                  ListBreakpoints / RecordBreakpointHit
//   • Timeline: RecordTimelineEvent / GetTimeline / ClearTimeline / MaxTimelineEvents
//   • Event browser: AttachEventBrowser / GetEventLog / ClearEventLog
#pragma once

#include "ChuckStation5/Cpu/Cpu.h"
#include "ChuckStation5/RuntimeEvents/RuntimeEvents.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ChuckStation5::Debugger {

// ── Register view types ───────────────────────────────────────────────────

struct RegEntry
{
    std::string name;
    uint64_t    value = 0;
};

struct FlagEntry
{
    std::string name;
    bool        set = false;
};

struct SpecialReg
{
    std::string name;
    uint64_t    value = 0;
};

// ── Call stack ────────────────────────────────────────────────────────────

struct CallFrame
{
    uint64_t    returnAddr = 0;
    uint64_t    frameBase  = 0;
    std::string symbol;
};

// ── Module entry ──────────────────────────────────────────────────────────

struct ModuleEntry
{
    std::string name;
    uint64_t    baseAddr = 0;
    size_t      size     = 0;
    uint32_t    id       = 0;
};

// ── Symbol entry (Phase 8 typed) ──────────────────────────────────────────

struct NearestSymbolResult
{
    std::string name;
    uint64_t    addr   = 0;
    uint64_t    offset = 0; ///< address - symbol base
};

// ── Breakpoint ────────────────────────────────────────────────────────────

struct BreakpointEntry
{
    uint32_t    id       = 0;
    uint64_t    addr     = 0;
    std::string label;
    uint32_t    hitCount = 0;
    bool        enabled  = true;
};

// ── Timeline ──────────────────────────────────────────────────────────────

struct TimelineEvent
{
    std::string category;
    uint64_t    address     = 0;
    uint64_t    timestampNs = 0;
};

// ── Event log entry ───────────────────────────────────────────────────────

struct EventLogEntry
{
    RuntimeEvents::EventType type{};
    uint64_t                 timestampNs = 0;
};

// ── Legacy types (kept for backward compatibility) ─────────────────────────

struct CpuState {
    uint64_t rip=0, rsp=0, rbp=0;
    uint64_t rax=0, rbx=0, rcx=0, rdx=0;
    uint64_t rsi=0, rdi=0;
    uint64_t r8=0,  r9=0,  r10=0, r11=0;
    uint64_t r12=0, r13=0, r14=0, r15=0;
    uint64_t rflags=0;
};

struct Breakpoint {
    uint64_t    address  = 0;
    bool        enabled  = true;
    std::string label;
    uint32_t    hitCount = 0;
};

struct StackFrame {
    uint64_t    rip   = 0;
    uint64_t    rsp   = 0;
    uint64_t    rbp   = 0;
    std::string label;
};

using BreakpointHitFn = std::function<void(const Breakpoint&, const CpuState&)>;
using StepFn          = std::function<void(uint64_t rip, const CpuState&)>;
using BreakConditionFn = std::function<bool(const CpuState&)>;

struct WatchExpression {
    uint32_t    id        = 0;
    std::string name;
    uint64_t    address   = 0;
    size_t      size      = 8;
    uint64_t    lastValue = 0;
    bool        changed   = false;
};

struct SymbolEntry {
    std::string   name;
    uint64_t      address  = 0;
    size_t        size     = 0;
    uint32_t      moduleId = 0;
    std::string   moduleName;
};

struct DebugEvent {
    uint64_t    timestampUs = 0;
    std::string type;
    std::string description;
    uint64_t    address     = 0;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();


std::vector<RegEntry>     GetRegisterView();
std::vector<SpecialReg>   GetSpecialRegisters();
std::vector<FlagEntry>    GetFlagsView();


std::vector<CallFrame>    GetCallStack(uint32_t maxDepth = 64);


std::vector<uint8_t>      ReadMemory(uint64_t address, size_t length);
void                      WriteMemory(uint64_t address, const std::vector<uint8_t>& data);
std::string               HexDump(uint64_t address, size_t length);


std::vector<ModuleEntry>  GetModuleList();


void                           AddSymbol(uint64_t addr, const std::string& name);
void                           RemoveSymbol(uint64_t addr);
std::optional<std::string>     LookupSymbol(uint64_t addr);
std::optional<NearestSymbolResult> NearestSymbol(uint64_t addr);


uint32_t                        SetBreakpoint(uint64_t addr, const std::string& label = "");
bool                            RemoveBreakpoint(uint32_t id);
void                            ClearAllBreakpoints();
std::vector<BreakpointEntry>    ListBreakpoints();
void                            RecordBreakpointHit(uint32_t id);


void                         RecordTimelineEvent(const std::string& category,
                                                 uint64_t address,
                                                 uint64_t timestampNs);
std::vector<TimelineEvent>   GetTimeline();
void                         ClearTimeline();
size_t                       MaxTimelineEvents();


void                         AttachEventBrowser();
std::vector<EventLogEntry>   GetEventLog();
std::vector<EventLogEntry>   GetEventLog(RuntimeEvents::EventType filter);
void                         ClearEventLog();

// ── Legacy API (maintained for backward compat) ───────────────────────────
uint32_t AddBreakpoint(uint64_t address, std::string label = "");
bool     EnableBreakpoint(uint32_t id, bool enable);
void     ClearBreakpoints();
uint32_t AddWatchpoint(uint64_t address, size_t size,
                       bool onRead = false, bool onWrite = true,
                       std::string label = "");
void     ClearWatchpoints();
void     Continue();
void     StepInto();
void     StepOver();
void     Pause();
bool     IsPaused();
std::optional<CpuState>  GetCpuState();
bool     ReadMemory(uint64_t address, void* buf, size_t size);
bool     WriteMemory(uint64_t address, const void* buf, size_t size);
std::string              HexDumpRegion(uint64_t address, size_t length);
std::vector<StackFrame>  GetStackTrace();
bool     WriteCrashDump(const std::string& dumpDir);
void     RegisterBreakpointCallback(BreakpointHitFn fn);
void     RegisterStepCallback(StepFn fn);
void     OnBreakpointHit(uint32_t id, const CpuState& state);
uint32_t AddConditionalBreakpoint(uint64_t address, BreakConditionFn condition,
                                   std::string label = "");
uint32_t                    AddWatch(std::string name, uint64_t address, size_t size = 8);
bool                        RemoveWatch(uint32_t id);
void                        UpdateWatches();
std::vector<WatchExpression> GetWatches();
std::vector<SymbolEntry>    BrowseSymbols(const std::string& filter = "");
std::string                 AddressToSymbol(uint64_t address);
std::vector<DebugEvent>     GetEventHistory(size_t maxEvents = 256);
void                        ClearEventHistory();

} // namespace ChuckStation5::Debugger
