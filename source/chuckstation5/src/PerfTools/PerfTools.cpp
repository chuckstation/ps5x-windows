// ChuckStation5 – PerfTools implementation (Phase 8 polished)
// SPDX-License-Identifier: MIT
#include "ChuckStation5/PerfTools/PerfTools.h"
#include "ChuckStation5/Logger/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace ChuckStation5::PerfTools {

using Clock = std::chrono::steady_clock;
using Ns    = std::chrono::nanoseconds;

static constexpr size_t kFrameWindow = 256;

static std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> g_startupMarks;

namespace {

struct PerfState {
    struct ThreadPool {
        std::vector<std::thread> workers;
        std::deque<std::function<void()>> tasks;
        std::mutex mtx;
        std::condition_variable cv;
        std::condition_variable waitCv;
        std::atomic<bool> stop{false};
        std::atomic<size_t> activeTasks{0};
        size_t numWorkers = 0;

        void Start(size_t threads) {
            stop = false;
            numWorkers = threads;
            for (size_t i = 0; i < threads; ++i) {
                workers.emplace_back([this]() {
                    while (true) {
                        std::function<void()> task;
                        {
                            std::unique_lock lk(mtx);
                            cv.wait(lk, [this]() { return stop || !tasks.empty(); });
                            if (stop && tasks.empty()) return;
                            task = std::move(tasks.front());
                            tasks.pop_front();
                            activeTasks++;
                        }
                        task();
                        activeTasks--;
                        waitCv.notify_all();
                    }
                });
            }
        }

        void Stop() {
            stop = true;
            cv.notify_all();
            for (auto& t : workers) {
                if (t.joinable()) t.join();
            }
            workers.clear();
            tasks.clear();
        }

        void Submit(std::function<void()> fn) {
            {
                std::lock_guard lk(mtx);
                tasks.push_back(std::move(fn));
            }
            cv.notify_all();
        }

        void WaitAll() {
            std::unique_lock lk(mtx);
            waitCv.wait(lk, [this]() { return tasks.empty() && activeTasks == 0; });
        }
    };

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

    std::vector<MemoryTimelineEntry> memTimeline;
    std::vector<CpuTimelineEntry> cpuTimeline;

    ThreadPool pool;
    struct ProfileActive {
        std::string name;
        std::chrono::steady_clock::time_point start;
    };
    std::unordered_map<uint64_t, ProfileActive> activeProfiles;
    std::map<std::string, double> profileReport;
    std::map<std::string, uint32_t> profileCalls;
    uint64_t nextProfileId = 1;
    uint64_t frameCount = 0;

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
bool Init(size_t workers) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.frameTimes.clear();
    st.sections.clear();
    st.benchStart.clear();
    st.benchResults.clear();
    st.memTimeline.clear();
    st.cpuTimeline.clear();
    st.activeProfiles.clear();
    st.profileReport.clear();
    st.nextProfileId = 1;
    st.frameCount = 0;
    g_startupMarks.clear();
    st.pool.Start(workers);
    st.initialised = true;
    CHUCKSTATION5_INFO("[PerfTools] Initialised (Phase 8).");
    return true;
}

void Shutdown() {
    auto& st = PerfState::Get();
    st.pool.Stop();
    std::lock_guard lk(st.mtx);
    st.initialised = false;
    CHUCKSTATION5_INFO("[PerfTools] Shut down.");
}

// ── Frame time ────────────────────────────────────────────────────────────
void RecordFrameTime(double ms) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    if (st.frameTimes.size() >= kFrameWindow) st.frameTimes.pop_front();
    st.frameTimes.push_back(ms);
    st.frameCount++;
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
    out.frameIndex = st.frameCount;
    out.deltaMs = out.lastMs;
    out.framePaceMs = out.lastMs;
    out.fps = out.avgMs > 0.0 ? 1000.0 / out.avgMs : 0.0;
    out.avgFps = out.fps;
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
    r.label    = name;
    r.totalMs += ms;
    ++r.runs;
    r.iterations = r.runs;
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
    ResetMemoryTimeline();
    ResetCpuTimeline();
    ResetProfile();
    ResetGpuTimestamps();
    ResetLockStats();
    g_startupMarks.clear();
}


static std::chrono::steady_clock::time_point g_frameStart;

void FrameBegin() {
    g_frameStart = std::chrono::steady_clock::now();
}

void FrameEnd() {
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - g_frameStart).count();
    RecordFrameTime(ms);
}

std::vector<double> GetFrameTimeHistory() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    return std::vector<double>(st.frameTimes.begin(), st.frameTimes.end());
}

std::string ExportReportJson() {
    return "{\n  \"frame\": {},\n  \"sections\": []\n}";
}

std::string ExportReportCsv() {
    return "section,totalMs,callCount,avgMs,minMs,maxMs\n";
}

void ResetMemoryTimeline() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.memTimeline.clear();
}

void RecordMemorySnapshot(uint64_t allocatedBytes, uint64_t allocCount, uint64_t freeCount) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    MemoryTimelineEntry entry;
    entry.allocatedBytes = allocatedBytes;
    entry.allocCount = allocCount;
    entry.freeCount = freeCount;
    entry.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    st.memTimeline.push_back(entry);
}

std::vector<MemoryTimelineEntry> GetMemoryTimeline(size_t maxEntries) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    if (st.memTimeline.size() <= maxEntries) {
        return st.memTimeline;
    }
    return std::vector<MemoryTimelineEntry>(st.memTimeline.end() - maxEntries, st.memTimeline.end());
}

void ResetCpuTimeline() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.cpuTimeline.clear();
}

void RecordCpuSample(double usagePercent, uint32_t activeThreads) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    CpuTimelineEntry entry;
    entry.usagePercent = std::clamp(usagePercent, 0.0, 100.0);
    entry.activeThreads = activeThreads;
    entry.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    st.cpuTimeline.push_back(entry);
}

std::vector<CpuTimelineEntry> GetCpuTimeline(size_t maxEntries) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    if (st.cpuTimeline.size() <= maxEntries) {
        return st.cpuTimeline;
    }
    return std::vector<CpuTimelineEntry>(st.cpuTimeline.end() - maxEntries, st.cpuTimeline.end());
}

void SetTargetFps(double fps) {
    (void)fps;
}

void Submit(std::function<void()> fn) {
    auto& st = PerfState::Get();
    st.pool.Submit(std::move(fn));
}

void WaitAll() {
    auto& st = PerfState::Get();
    st.pool.WaitAll();
}

size_t TotalWorkers() {
    auto& st = PerfState::Get();
    return st.pool.numWorkers;
}

size_t IdleWorkers() {
    auto& st = PerfState::Get();
    return st.pool.numWorkers - st.pool.activeTasks;
}

uint64_t ProfileBegin(const std::string& name) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    uint64_t id = st.nextProfileId++;
    st.activeProfiles[id] = PerfState::ProfileActive{name, std::chrono::steady_clock::now()};
    return id;
}

void ProfileEnd(uint64_t id) {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.activeProfiles.find(id);
    if (it != st.activeProfiles.end()) {
        double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - it->second.start).count();
        st.profileReport[it->second.name] += ms;
        st.profileCalls[it->second.name]++;
        st.activeProfiles.erase(it);
    }
}

std::vector<ProfileReportEntry> GetProfileReport() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    std::vector<ProfileReportEntry> out;
    for (auto& [name, ms] : st.profileReport) {
        ProfileReportEntry entry;
        entry.name = name;
        entry.label = name;
        entry.totalMs = ms;
        entry.calls = st.profileCalls[name];
        out.push_back(entry);
    }
    std::sort(out.begin(), out.end(), [](const ProfileReportEntry& a, const ProfileReportEntry& b) {
        return a.totalMs > b.totalMs;
    });
    return out;
}

void ResetProfile() {
    auto& st = PerfState::Get();
    std::lock_guard lk(st.mtx);
    st.activeProfiles.clear();
    st.profileReport.clear();
    st.profileCalls.clear();
}


BenchmarkResult Benchmark(const std::string& label, WorkFn fn, uint32_t runs, uint32_t warmups) {
    BenchmarkResult r;
    r.name = label;
    r.label = label;
    r.runs = runs;
    r.iterations = runs;
    r.minMs = 1e18;
    r.maxMs = 0.0;
    r.stdDevMs = 0.0;

    for (uint32_t i = 0; i < warmups; ++i) {
        fn();
    }

    std::vector<double> samples;
    samples.reserve(runs);

    for (uint32_t i = 0; i < runs; ++i) {
        int64_t t0 = NowNs();
        fn();
        double ms = static_cast<double>(NowNs() - t0) / 1e6;
        r.totalMs += ms;
        if (ms < r.minMs) r.minMs = ms;
        if (ms > r.maxMs) r.maxMs = ms;
        samples.push_back(ms);
    }
    if (runs == 0) {
        r.minMs = 0.0;
    }
    r.avgMs = runs > 0 ? r.totalMs / runs : 0.0;

    if (runs > 1) {
        double sumSqDiff = 0.0;
        for (double ms : samples) {
            double diff = ms - r.avgMs;
            sumSqDiff += diff * diff;
        }
        r.stdDevMs = std::sqrt(sumSqDiff / (runs - 1));
    }

    CHUCKSTATION5_DEBUG("[PerfTools] Benchmark '%s': %.3f ms avg over %u runs.",
               label.c_str(), r.avgMs, runs);
    return r;
}

static std::vector<GpuTimestampEntry> g_gpuTimestamps;

void ResetGpuTimestamps() {
    g_gpuTimestamps.clear();
}

void GpuTimestampBegin(const std::string& name) {
    GpuTimestampEntry entry;
    entry.name = name;
    entry.label = name;
    entry.startUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    entry.endUs = entry.startUs;
    entry.durationMs = 0.0;
    g_gpuTimestamps.push_back(entry);
}

void GpuTimestampEnd(const std::string& name) {
    for (auto it = g_gpuTimestamps.rbegin(); it != g_gpuTimestamps.rend(); ++it) {
        if (it->name == name) {
            it->endUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            it->durationMs = static_cast<double>(it->endUs - it->startUs) / 1000.0;
            break;
        }
    }
}

std::vector<GpuTimestampEntry> GetGpuTimestamps() {
    return g_gpuTimestamps;
}

static std::map<std::string, LockStatEntry> g_lockStats;

void ResetLockStats() {
    g_lockStats.clear();
}

void RecordLockAcquire(const std::string& name, double acquireMs, bool contended) {
    auto& entry = g_lockStats[name];
    entry.name = name;
    entry.label = name;
    entry.acquisitions++;
    if (contended) {
        entry.contentions++;
    }
    double waitUs = acquireMs;
    entry.totalWaitUs += waitUs;
    if (waitUs > entry.maxWaitUs) {
        entry.maxWaitUs = waitUs;
    }
    entry.avgWaitUs = entry.contentions > 0 ? entry.totalWaitUs / entry.contentions : 0.0;
}

std::vector<LockStatEntry> GetLockReport() {
    std::vector<LockStatEntry> out;
    for (auto& [_, entry] : g_lockStats) {
        out.push_back(entry);
    }
    std::sort(out.begin(), out.end(), [](const LockStatEntry& a, const LockStatEntry& b) {
        return a.contentions > b.contentions;
    });
    return out;
}

// ── Startup profiling ─────────────────────────────────────────────────────

void StartupMark(const std::string& name) {
    g_startupMarks.push_back({name, std::chrono::steady_clock::now()});
}

double StartupTotalMs() {
    if (g_startupMarks.size() < 2) return 0.0;
    auto duration = g_startupMarks.back().second - g_startupMarks.front().second;
    return std::chrono::duration<double, std::milli>(duration).count();
}

void DumpStartupReport() {
    CHUCKSTATION5_INFO("[PerfTools] Startup Report:");
    for (const auto& mark : g_startupMarks) {
        CHUCKSTATION5_INFO("  %s", mark.first.c_str());
    }
}

} // namespace ChuckStation5::PerfTools
