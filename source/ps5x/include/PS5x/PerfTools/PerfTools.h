// PS5x – PerfTools module (Phase 8 polished)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
//
// Phase 8 additions:
//   BeginBenchmark / EndBenchmark / GetBenchmarkResults
//   ScopeTimer class, GetSectionStats, FrameWindowSize
//   ResetAll (clears frame stats + section stats + benchmarks)
//   GetAverageFPS
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace PS5x::PerfTools {

// ── Phase 6 Timeline Entries ──────────────────────────────────────────────
struct MemoryTimelineEntry {
    uint64_t allocatedBytes = 0;
    uint64_t allocCount     = 0;
    uint64_t freeCount      = 0;
    uint64_t timestampUs    = 0;
};

struct CpuTimelineEntry {
    double   usagePercent  = 0.0;
    uint32_t activeThreads = 0;
    uint64_t timestampUs    = 0;
};

// ── Frame statistics ──────────────────────────────────────────────────────
struct FrameStats {
    size_t   samples = 0;
    double   avgMs   = 0.0;
    double   minMs   = 0.0;
    double   maxMs   = 0.0;
    double   lastMs  = 0.0;
    uint64_t frameIndex = 0;
    double   framePaceMs = 0.0;
    double   deltaMs = 0.0;
    double   fps = 0.0;
    double   avgFps = 0.0;
};

// ── Profile report entry ──────────────────────────────────────────────────
struct ProfileReportEntry {
    std::string name;
    std::string label;
    double      totalMs = 0.0;
    uint32_t    calls = 0;
};

// ── Section statistics ────────────────────────────────────────────────────
struct SectionStat {
    std::string name;
    double      totalMs   = 0.0;
    uint64_t    callCount = 0;
    double      avgMs     = 0.0;
    double      minMs     = 0.0;
    double      maxMs     = 0.0;
};

// ── Benchmark result ──────────────────────────────────────────────────────
struct BenchmarkResult {
    std::string name;
    std::string label;
    double      totalMs  = 0.0;
    uint64_t    runs     = 0;
    uint64_t    iterations = 0;
    double      avgMs    = 0.0;
    double      minMs    = 0.0;
    double      maxMs    = 0.0;
    double      stdDevMs = 0.0;
};

// ── Phase 6: WorkFn benchmark ─────────────────────────────────────────────
using WorkFn = std::function<void()>;

// ── RAII scope timer ──────────────────────────────────────────────────────
class ScopeTimer {
public:
    explicit ScopeTimer(const std::string& name);
    ~ScopeTimer();
private:
    std::string name_;
    int64_t     startNs_ = 0;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(size_t workers = 2);
void Shutdown();

// ── Frame time ────────────────────────────────────────────────────────────
void       RecordFrameTime(double ms);
FrameStats GetFrameStats();
double     GetAverageFPS();
size_t     FrameWindowSize();
void       ResetFrameStats();
void       SetTargetFps(double fps);

// ── Section timers ────────────────────────────────────────────────────────
void                      RecordSection(const std::string& name, double ms);
std::vector<SectionStat>  GetSectionStats();
void                      ResetSectionStats();

// ── Named benchmarks ─────────────────────────────────────────────────────
void                          BeginBenchmark(const std::string& name);
double                        EndBenchmark(const std::string& name);
std::vector<BenchmarkResult>  GetBenchmarkResults();
void                          ResetBenchmarks();

// ── Startup profiling ─────────────────────────────────────────────────────
void StartupMark(const std::string& name);
double StartupTotalMs();
void DumpStartupReport();

// ── Thread Pool / Worker Pool ─────────────────────────────────────────────
void Submit(std::function<void()> fn);
void WaitAll();
size_t TotalWorkers();
size_t IdleWorkers();

// ── Profile ───────────────────────────────────────────────────────────────
uint64_t ProfileBegin(const std::string& name);
void     ProfileEnd(uint64_t id);
std::vector<ProfileReportEntry> GetProfileReport();
void     ResetProfile();

// ── Compound reset ────────────────────────────────────────────────────────
void ResetAll();

// ── Phase 6 Extensions ────────────────────────────────────────────────────
void ResetMemoryTimeline();
void RecordMemorySnapshot(uint64_t allocatedBytes, uint64_t allocCount, uint64_t freeCount);
std::vector<MemoryTimelineEntry> GetMemoryTimeline(size_t maxEntries = 64);

void ResetCpuTimeline();
void RecordCpuSample(double usagePercent, uint32_t activeThreads);
std::vector<CpuTimelineEntry> GetCpuTimeline(size_t maxEntries = 64);

std::vector<double> GetFrameTimeHistory();
void FrameBegin();
void FrameEnd();

std::string ExportReportJson();
std::string ExportReportCsv();

struct GpuTimestampEntry {
    std::string name;
    std::string label;
    double      durationMs = 0.0;
    uint64_t    startUs = 0;
    uint64_t    endUs = 0;
};
void ResetGpuTimestamps();
void GpuTimestampBegin(const std::string& name);
void GpuTimestampEnd(const std::string& name);
std::vector<GpuTimestampEntry> GetGpuTimestamps();

struct LockStatEntry {
    std::string name;
    std::string label;
    uint64_t    acquisitions = 0;
    uint64_t    contentions = 0;
    double      totalWaitUs = 0.0;
    double      avgWaitUs = 0.0;
    double      maxWaitUs = 0.0;
};
void ResetLockStats();
void RecordLockAcquire(const std::string& name, double acquireMs, bool contended);
std::vector<LockStatEntry> GetLockReport();

class ScopeGuard {
public:
    explicit ScopeGuard(const std::string& name) {
        token_ = ProfileBegin(name);
    }
    ~ScopeGuard() {
        ProfileEnd(token_);
    }
private:
    uint64_t token_;
};

// ── Phase 6: WorkFn benchmark ─────────────────────────────────────────────
BenchmarkResult Benchmark(const std::string& label, WorkFn fn,
                           uint32_t runs = 1, uint32_t warmups = 0);

} // namespace PS5x::PerfTools
