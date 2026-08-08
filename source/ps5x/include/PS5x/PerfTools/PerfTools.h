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

// ── Frame statistics ──────────────────────────────────────────────────────
struct FrameStats {
    size_t  samples = 0;
    double  avgMs   = 0.0;
    double  minMs   = 0.0;
    double  maxMs   = 0.0;
    double  lastMs  = 0.0;
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
    double      totalMs  = 0.0;
    uint64_t    runs     = 0;
    double      avgMs    = 0.0;
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
bool Init();
void Shutdown();

// ── Frame time ────────────────────────────────────────────────────────────
void       RecordFrameTime(double ms);
FrameStats GetFrameStats();
double     GetAverageFPS();
size_t     FrameWindowSize();
void       ResetFrameStats();

// ── Section timers ────────────────────────────────────────────────────────
void                      RecordSection(const std::string& name, double ms);
std::vector<SectionStat>  GetSectionStats();
void                      ResetSectionStats();

// ── Named benchmarks ─────────────────────────────────────────────────────
void                          BeginBenchmark(const std::string& name);
double                        EndBenchmark(const std::string& name);
std::vector<BenchmarkResult>  GetBenchmarkResults();
void                          ResetBenchmarks();

// ── Compound reset ────────────────────────────────────────────────────────
void ResetAll();

// ── Phase 6: WorkFn benchmark ─────────────────────────────────────────────
BenchmarkResult Benchmark(const std::string& label, WorkFn fn,
                           uint32_t runs = 1);

} // namespace PS5x::PerfTools
