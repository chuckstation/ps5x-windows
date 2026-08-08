// PS5x – Shader Cache implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/ShaderCache/ShaderCache.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace PS5x::ShaderCache {

using Clock = std::chrono::steady_clock;

static uint64_t NowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
}

// ── Key hash for unordered_map ─────────────────────────────────────────────
struct KeyHash {
    size_t operator()(const ShaderKey& k) const {
        size_t h = std::hash<uint64_t>{}(k.spirvHash);
        h ^= std::hash<uint64_t>{}(k.pipelineHash) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(k.stage));
        return h;
    }
};

// ── Background compile job ─────────────────────────────────────────────────
struct CompileJob {
    ShaderKey             key;
    std::vector<uint8_t>  spirv;
    std::string           debugName;
};

// ── State ─────────────────────────────────────────────────────────────────
namespace {

struct CacheState {
    std::unordered_map<ShaderKey, CacheEntry, KeyHash> entries;
    size_t                    maxEntries  = 4096;
    std::filesystem::path     cacheDir;
    CompileFn                 compileFn;
    std::mutex                mtx;

    // Background worker
    std::deque<CompileJob>    jobQueue;
    std::mutex                jobMtx;
    std::condition_variable   jobCv;
    std::thread               worker;
    std::atomic<bool>         workerRunning{false};
    std::atomic<uint32_t>     pendingJobs{0};

    // Stats
    std::atomic<uint64_t>     hits{0};
    std::atomic<uint64_t>     misses{0};
    std::atomic<uint64_t>     evictions{0};
    std::atomic<uint64_t>     compilations{0};
    std::atomic<double>       totalCompileMs{0.0};

    static CacheState& Get() { static CacheState s; return s; }
};

void RunWorker()
{
    auto& cs = CacheState::Get();
    while (cs.workerRunning.load()) {
        CompileJob job;
        {
            std::unique_lock ul(cs.jobMtx);
            cs.jobCv.wait(ul, [&]{
                return !cs.jobQueue.empty() || !cs.workerRunning.load();
            });
            if (!cs.workerRunning.load() && cs.jobQueue.empty()) break;
            if (cs.jobQueue.empty()) continue;
            job = std::move(cs.jobQueue.front());
            cs.jobQueue.pop_front();
        }

        // Compile
        std::vector<uint8_t> binary;
        double ms = 0.0;
        if (cs.compileFn) {
            auto t0 = Clock::now();
            binary = cs.compileFn(job.spirv, job.key.stage);
            ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        }

        if (!binary.empty()) {
            CacheEntry entry;
            entry.key           = job.key;
            entry.binary        = std::move(binary);
            entry.spirv         = job.spirv;
            entry.compiledAtUs  = NowUs();
            entry.lastUsedUs    = entry.compiledAtUs;
            entry.hitCount      = 0;
            entry.compilationMs = ms;
            entry.debugName     = job.debugName;
            entry.valid         = true;

            std::lock_guard lk(cs.mtx);
            // Evict LRU if at capacity
            if (cs.entries.size() >= cs.maxEntries) {
                auto oldest = cs.entries.begin();
                for (auto it = cs.entries.begin(); it != cs.entries.end(); ++it)
                    if (it->second.lastUsedUs < oldest->second.lastUsedUs) oldest = it;
                cs.entries.erase(oldest);
                cs.evictions.fetch_add(1, std::memory_order_relaxed);
            }
            cs.entries[job.key] = std::move(entry);
            cs.compilations.fetch_add(1, std::memory_order_relaxed);
            double cur = cs.totalCompileMs.load();
            cs.totalCompileMs.store(cur + ms);

            RuntimeEvents::Publish(RuntimeEvents::EventType::ShaderCompiled,
                RuntimeEvents::CustomPayload{"ShaderCompiled", job.debugName});
            PS5X_DEBUG("[ShaderCache] Compiled '%s' in %.2f ms", job.debugName.c_str(), ms);
        } else {
            PS5X_WARN("[ShaderCache] Compilation failed for '%s'", job.debugName.c_str());
        }
        cs.pendingJobs.fetch_sub(1, std::memory_order_relaxed);
    }
}

} // anonymous namespace

// ── Name table ─────────────────────────────────────────────────────────────
const char* StageName(ShaderStage s)
{
    switch (s) {
        case ShaderStage::Vertex:   return "Vertex";
        case ShaderStage::Fragment: return "Fragment";
        case ShaderStage::Compute:  return "Compute";
        case ShaderStage::Geometry: return "Geometry";
        case ShaderStage::TessCtrl: return "TessCtrl";
        case ShaderStage::TessEval: return "TessEval";
    }
    return "Unknown";
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(const std::filesystem::path& cacheDir, size_t maxEntries)
{
    auto& cs = CacheState::Get();
    {
        std::lock_guard lk(cs.mtx);
        cs.entries.clear();
        cs.maxEntries = maxEntries;
        cs.cacheDir   = cacheDir;
    }
    cs.compileFn = nullptr;  // reset compiler; caller must call SetCompiler again
    cs.hits.store(0); cs.misses.store(0);
    cs.evictions.store(0); cs.compilations.store(0);
    cs.totalCompileMs.store(0.0); cs.pendingJobs.store(0);

    // Start background worker
    cs.workerRunning.store(true);
    cs.worker = std::thread(RunWorker);

    // Load from disk if dir given
    if (!cacheDir.empty() && std::filesystem::exists(cacheDir / "shadercache.bin"))
        LoadFromDisk(cacheDir / "shadercache.bin");

    PS5X_INFO("[ShaderCache] Init: maxEntries=%zu dir=%s",
              maxEntries, cacheDir.empty() ? "(none)" : cacheDir.string().c_str());
    return true;
}

void Shutdown()
{
    auto& cs = CacheState::Get();
    FlushQueue();
    cs.workerRunning.store(false);
    cs.jobCv.notify_all();
    if (cs.worker.joinable()) cs.worker.join();

    if (!cs.cacheDir.empty())
        SaveToDisk(cs.cacheDir / "shadercache.bin");

    std::lock_guard lk(cs.mtx);
    cs.entries.clear();
    PS5X_INFO("[ShaderCache] Shutdown. Compiled=%llu hits=%llu misses=%llu",
              static_cast<unsigned long long>(cs.compilations.load()),
              static_cast<unsigned long long>(cs.hits.load()),
              static_cast<unsigned long long>(cs.misses.load()));
}

// ── Compilation ───────────────────────────────────────────────────────────
void SetCompiler(CompileFn fn)
{
    CacheState::Get().compileFn = std::move(fn);
    PS5X_INFO("[ShaderCache] Compiler registered.");
}

uint64_t HashSpirv(const uint8_t* data, size_t size)
{
    // FNV-1a 64-bit
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(data[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t HashPipelineState(const uint8_t* data, size_t size)
{
    return HashSpirv(data, size); // same algorithm, different domain
}

std::optional<CacheEntry> Lookup(const ShaderKey& key)
{
    auto& cs = CacheState::Get();
    std::lock_guard lk(cs.mtx);
    auto it = cs.entries.find(key);
    if (it == cs.entries.end()) {
        cs.misses.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    it->second.hitCount++;
    it->second.lastUsedUs = NowUs();
    cs.hits.fetch_add(1, std::memory_order_relaxed);
    return it->second;
}

std::optional<CacheEntry> Compile(const ShaderKey& key,
                                   const std::vector<uint8_t>& spirv,
                                   const std::string& debugName)
{
    auto& cs = CacheState::Get();

    // Check cache first
    if (auto hit = Lookup(key)) return hit;

    // Compile synchronously
    std::vector<uint8_t> binary;
    double ms = 0.0;
    if (cs.compileFn) {
        auto t0 = Clock::now();
        binary = cs.compileFn(spirv, key.stage);
        ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    } else {
        // No compiler – store SPIR-V as placeholder binary
        binary = spirv;
        PS5X_DEBUG("[ShaderCache] No compiler set; storing raw SPIR-V for '%s'",
                   debugName.c_str());
    }

    CacheEntry entry;
    entry.key           = key;
    entry.binary        = binary;
    entry.spirv         = spirv;
    entry.compiledAtUs  = NowUs();
    entry.lastUsedUs    = entry.compiledAtUs;
    entry.compilationMs = ms;
    entry.debugName     = debugName;
    entry.valid         = !binary.empty();

    {
        std::lock_guard lk(cs.mtx);
        if (cs.entries.size() >= cs.maxEntries) {
            auto oldest = cs.entries.begin();
            for (auto it = cs.entries.begin(); it != cs.entries.end(); ++it)
                if (it->second.lastUsedUs < oldest->second.lastUsedUs) oldest = it;
            cs.entries.erase(oldest);
            cs.evictions.fetch_add(1, std::memory_order_relaxed);
        }
        cs.entries[key] = entry;
    }
    cs.compilations.fetch_add(1, std::memory_order_relaxed);
    double cur = cs.totalCompileMs.load();
    cs.totalCompileMs.store(cur + ms);

    RuntimeEvents::Publish(RuntimeEvents::EventType::ShaderCompiled,
        RuntimeEvents::CustomPayload{"ShaderCompiled", debugName});
    PS5X_INFO("[ShaderCache] Compiled '%s' [%s] %.2f ms binary=%zu bytes",
              debugName.c_str(), StageName(key.stage), ms, binary.size());

    return entry;
}

void QueueCompile(const ShaderKey& key, const std::vector<uint8_t>& spirv,
                  const std::string& debugName)
{
    auto& cs = CacheState::Get();
    cs.pendingJobs.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lk(cs.jobMtx);
        cs.jobQueue.push_back({key, spirv, debugName});
    }
    cs.jobCv.notify_one();
}

void FlushQueue()
{
    auto& cs = CacheState::Get();
    while (cs.pendingJobs.load() > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

uint32_t PendingCount() { return CacheState::Get().pendingJobs.load(); }

// ── Cache management ──────────────────────────────────────────────────────
bool Invalidate(const ShaderKey& key)
{
    auto& cs = CacheState::Get();
    std::lock_guard lk(cs.mtx);
    return cs.entries.erase(key) > 0;
}

uint32_t InvalidateAll()
{
    auto& cs = CacheState::Get();
    std::lock_guard lk(cs.mtx);
    uint32_t n = static_cast<uint32_t>(cs.entries.size());
    cs.entries.clear();
    PS5X_INFO("[ShaderCache] InvalidateAll: removed %u entries", n);
    return n;
}

bool SaveToDisk(const std::filesystem::path& path)
{
    auto& cs = CacheState::Get();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        PS5X_ERROR("[ShaderCache] SaveToDisk failed: %s", path.string().c_str());
        return false;
    }

    std::lock_guard lk(cs.mtx);
    // Simple flat format: [count][entry...] each entry: [key][name_len][name][binary_len][binary]
    uint32_t count = static_cast<uint32_t>(cs.entries.size());
    f.write(reinterpret_cast<const char*>(&count), 4);
    for (const auto& [k, e] : cs.entries) {
        f.write(reinterpret_cast<const char*>(&k), sizeof(k));
        uint32_t nl = static_cast<uint32_t>(e.debugName.size());
        f.write(reinterpret_cast<const char*>(&nl), 4);
        f.write(e.debugName.data(), nl);
        uint32_t bl = static_cast<uint32_t>(e.binary.size());
        f.write(reinterpret_cast<const char*>(&bl), 4);
        f.write(reinterpret_cast<const char*>(e.binary.data()), bl);
        f.write(reinterpret_cast<const char*>(&e.compilationMs), sizeof(double));
        f.write(reinterpret_cast<const char*>(&e.compiledAtUs), sizeof(uint64_t));
    }
    PS5X_INFO("[ShaderCache] Saved %u entries to %s", count, path.string().c_str());
    return true;
}

bool LoadFromDisk(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    auto& cs = CacheState::Get();
    uint32_t count = 0;
    f.read(reinterpret_cast<char*>(&count), 4);

    std::lock_guard lk(cs.mtx);
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < count; ++i) {
        ShaderKey k{};
        f.read(reinterpret_cast<char*>(&k), sizeof(k));
        uint32_t nl = 0; f.read(reinterpret_cast<char*>(&nl), 4);
        std::string name(nl, '\0');
        f.read(name.data(), nl);
        uint32_t bl = 0; f.read(reinterpret_cast<char*>(&bl), 4);
        std::vector<uint8_t> bin(bl);
        f.read(reinterpret_cast<char*>(bin.data()), bl);
        double ms = 0.0; f.read(reinterpret_cast<char*>(&ms), sizeof(double));
        uint64_t ts = 0; f.read(reinterpret_cast<char*>(&ts), sizeof(uint64_t));

        if (!f) break;
        CacheEntry e;
        e.key=k; e.binary=std::move(bin); e.debugName=std::move(name);
        e.compilationMs=ms; e.compiledAtUs=ts; e.lastUsedUs=ts; e.valid=true;
        cs.entries[k] = std::move(e);
        loaded++;
    }
    PS5X_INFO("[ShaderCache] Loaded %u/%u entries from disk.", loaded, count);
    return loaded > 0;
}

uint32_t Evict(uint64_t maxAgeUs)
{
    auto& cs = CacheState::Get();
    uint64_t cutoff = NowUs() - maxAgeUs;
    std::lock_guard lk(cs.mtx);
    uint32_t n = 0;
    for (auto it = cs.entries.begin(); it != cs.entries.end(); ) {
        if (it->second.lastUsedUs < cutoff) {
            it = cs.entries.erase(it);
            n++;
        } else ++it;
    }
    cs.evictions.fetch_add(n, std::memory_order_relaxed);
    if (n) PS5X_INFO("[ShaderCache] Evicted %u stale entries.", n);
    return n;
}

// ── Statistics ─────────────────────────────────────────────────────────────
CacheStats GetStats()
{
    auto& cs = CacheState::Get();
    CacheStats s;
    std::lock_guard lk(cs.mtx);
    s.entries        = static_cast<uint32_t>(cs.entries.size());
    s.hits           = cs.hits.load();
    s.misses         = cs.misses.load();
    s.evictions      = cs.evictions.load();
    s.compilations   = cs.compilations.load();
    s.totalCompileMs = cs.totalCompileMs.load();
    s.avgCompileMs   = s.compilations ? s.totalCompileMs / s.compilations : 0.0;
    s.pendingJobs    = cs.pendingJobs.load();
    return s;
}

void DumpStats()
{
    auto s = GetStats();
    PS5X_INFO("[ShaderCache] entries=%u hits=%llu misses=%llu evictions=%llu",
              s.entries,
              static_cast<unsigned long long>(s.hits),
              static_cast<unsigned long long>(s.misses),
              static_cast<unsigned long long>(s.evictions));
    PS5X_INFO("[ShaderCache] compilations=%llu totalMs=%.1f avgMs=%.2f pending=%u",
              static_cast<unsigned long long>(s.compilations),
              s.totalCompileMs, s.avgCompileMs, s.pendingJobs);
}

} // namespace PS5x::ShaderCache
