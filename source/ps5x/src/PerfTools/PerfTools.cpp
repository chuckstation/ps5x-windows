// PS5x – PerfTools implementation (Phase 8 polished)
// SPDX-License-Identifier: MIT
#include "PS5x/PerfTools/PerfTools.h"
#include "PS5x/Logger/Logger.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>

namespace PS5x::PerfTools {

using Clock = std::chrono::steady_clock;
using Ns    = std::chrono::nanoseconds;

static constexpr size_t kFrameWindow = 256;

namespace {

struct PerfState {
    std::mutex mtx;
    bool       initialised = false;

    // Frame times (rolling window)
    std::deque<double> frameTimes;

    // Section timers
    struct SecEntry { double totalMs=0; uint64_t count=0; double minMs=1e18; double maxMs=0; };
    std::map<std::string, SecEntry> sections;

    // Active benchmark start times
    std::map<std::string, int64_t> benchStart; // name → start ns

    // Completed benchmark results
    std::map<std::string, BenchmarkResult> benchResults;

    static PerfState& Get() { static PerfState s; return s; }
};

static int64_t NowNs() {
    return std::chrono::duration_cast<Ns>(Clock::now().time_since_epoch()).count();
}

} // namespace

// ── ScopeTimer ────────────────────────────────────────────────────────────
ScopeTimer::ScopeTimer(const std::string& name)
    : name_(name), startNs_(NowNs()) {}

ScopeTimer::~ScopeTimer() {
    double ms = static_cast<double>(NowNs() - startNs_) / 1e6;
    RecordSection(name_, ms);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.frameTimes.clear();
    st.sections.clear();
    st.benchStart.clear();
    st.benchResults.clear();
    st.initialised = true;
    PS5X_INFO("[PerfTools] Initialised (Phase 8).");
    return true;
}

void Shutdown() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.initialised = false;
    PS5X_INFO("[PerfTools] Shut down.");
}

// ── Frame time ────────────────────────────────────────────────────────────
void RecordFrameTime(double ms) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    if (st.frameTimes.size() >= kFrameWindow) st.frameTimes.pop_front();
    st.frameTimes.push_back(ms);
}

FrameStats GetFrameStats() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    FrameStats out;
    if (st.frameTimes.empty()) return out;
    out.samples = st.frameTimes.size();
    out.lastMs  = st.frameTimes.back();
    double sum = 0, mn = 1e18, mx = 0;
    for (double v : st.frameTimes) {
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    out.avgMs = sum / out.samples;
    out.minMs = mn;
    out.maxMs = mx;
    return out;
}

double GetAverageFPS() {
    auto stats = GetFrameStats();
    if (stats.samples == 0 || stats.avgMs <= 0.0) return 0.0;
    return 1000.0 / stats.avgMs;
}

size_t FrameWindowSize() { return kFrameWindow; }

void ResetFrameStats() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.frameTimes.clear();
}

// ── Section timers ────────────────────────────────────────────────────────
void RecordSection(const std::string& name, double ms) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    auto& e = st.sections[name];
    e.totalMs += ms;
    ++e.count;
    if (ms < e.minMs) e.minMs = ms;
    if (ms > e.maxMs) e.maxMs = ms;
}

std::vector<SectionStat> GetSectionStats() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    std::vector<SectionStat> out;
    for (auto& [name, e] : st.sections) {
        SectionStat s;
        s.name      = name;
        s.totalMs   = e.totalMs;
        s.callCount = e.count;
        s.avgMs     = e.count > 0 ? e.totalMs / e.count : 0.0;
        s.minMs     = e.minMs < 1e17 ? e.minMs : 0.0;
        s.maxMs     = e.maxMs;
        out.push_back(s);
    }
    return out;
}

void ResetSectionStats() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.sections.clear();
}

// ── Named benchmarks ─────────────────────────────────────────────────────
void BeginBenchmark(const std::string& name) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.benchStart[name] = NowNs();
}

double EndBenchmark(const std::string& name) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.benchStart.find(name);
    if (it == st.benchStart.end()) return 0.0;
    double ms = static_cast<double>(NowNs() - it->second) / 1e6;
    st.benchStart.erase(it);
    auto& r = st.benchResults[name];
    r.name     = name;
    r.totalMs += ms;
    ++r.runs;
    r.avgMs    = r.totalMs / r.runs;
    return ms;
}

std::vector<BenchmarkResult> GetBenchmarkResults() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    std::vector<BenchmarkResult> out;
    for (auto& [_, r] : st.benchResults) out.push_back(r);
    return out;
}

void ResetBenchmarks() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.benchStart.clear();
    st.benchResults.clear();
}

// ── Compound reset ────────────────────────────────────────────────────────
void ResetAll() {
    ResetFrameStats();
    ResetSectionStats();
    ResetBenchmarks();
}

// ── Phase 6: WorkFn benchmark ─────────────────────────────────────────────
BenchmarkResult Benchmark(const std::string& label, WorkFn fn, uint32_t runs) {
    BenchmarkResult r;
    r.name = label;
    r.runs = runs;
    for (uint32_t i = 0; i < runs; ++i) {
        int64_t t0 = NowNs();
        fn();
        double ms = static_cast<double>(NowNs() - t0) / 1e6;
        r.totalMs += ms;
    }
    r.avgMs = runs > 0 ? r.totalMs / runs : 0.0;
    PS5X_DEBUG("[PerfTools] Benchmark '%s': %.3f ms avg over %u runs.",
               label.c_str(), r.avgMs, runs);
    return r;
}

} // namespace PS5x::PerfTools
