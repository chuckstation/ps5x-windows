// PS5x – Shader Cache
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// Binary shader cache with pipeline serialisation, cache invalidation,
// statistics, and a background compilation queue.
// Shaders are identified by a hash of their SPIR-V source + pipeline state.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace PS5x::ShaderCache {

// ── Shader stage ──────────────────────────────────────────────────────────
enum class ShaderStage : uint8_t
{
    Vertex   = 0,
    Fragment = 1,
    Compute  = 2,
    Geometry = 3,
    TessCtrl = 4,
    TessEval = 5,
};
const char* StageName(ShaderStage s);

// ── Shader key ────────────────────────────────────────────────────────────
/// Uniquely identifies a shader variant: hash of SPIR-V + pipeline state hash.
struct ShaderKey
{
    uint64_t spirvHash     = 0;
    uint64_t pipelineHash  = 0;
    ShaderStage stage      = ShaderStage::Vertex;

    bool operator==(const ShaderKey& o) const {
        return spirvHash == o.spirvHash &&
               pipelineHash == o.pipelineHash &&
               stage == o.stage;
    }
};

// ── Compiled shader entry ─────────────────────────────────────────────────
struct CacheEntry
{
    ShaderKey             key;
    std::vector<uint8_t>  binary;        ///< platform-specific compiled binary
    std::vector<uint8_t>  spirv;         ///< original SPIR-V (for re-compilation)
    uint64_t              compiledAtUs   = 0;
    uint64_t              lastUsedUs     = 0;
    uint32_t              hitCount       = 0;
    double                compilationMs  = 0.0;
    std::string           debugName;
    bool                  valid          = false;
};

// ── Statistics ────────────────────────────────────────────────────────────
struct CacheStats
{
    uint32_t  entries          = 0;
    uint64_t  hits             = 0;
    uint64_t  misses           = 0;
    uint64_t  evictions        = 0;
    uint64_t  compilations     = 0;
    double    totalCompileMs   = 0.0;
    double    avgCompileMs     = 0.0;
    size_t    diskBytes        = 0;
    uint32_t  pendingJobs      = 0;
};

// ── Compilation callback ──────────────────────────────────────────────────
/// Returns compiled binary bytes on success, empty on failure.
using CompileFn = std::function<std::vector<uint8_t>(
    const std::vector<uint8_t>& spirv, ShaderStage stage)>;

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(const std::filesystem::path& cacheDir = "",
          size_t maxEntries = 4096);
void Shutdown();

// ── Compilation ───────────────────────────────────────────────────────────
/// Register the platform compilation function (e.g. glslang/DXC wrapper).
void SetCompiler(CompileFn fn);

/// Hash a SPIR-V blob (FNV-1a 64-bit).
uint64_t HashSpirv(const uint8_t* data, size_t size);

/// Hash pipeline state bytes.
uint64_t HashPipelineState(const uint8_t* data, size_t size);

/// Look up a shader. Returns nullopt on miss.
std::optional<CacheEntry> Lookup(const ShaderKey& key);

/// Compile and cache a shader synchronously.
/// Returns the cache entry on success.
std::optional<CacheEntry> Compile(const ShaderKey& key,
                                   const std::vector<uint8_t>& spirv,
                                   const std::string& debugName = "");

/// Submit a shader for background compilation.
/// The entry is inserted into the cache when ready.
void QueueCompile(const ShaderKey& key,
                  const std::vector<uint8_t>& spirv,
                  const std::string& debugName = "");

/// Block until all queued compilations complete.
void FlushQueue();

/// Number of pending background jobs.
uint32_t PendingCount();

// ── Cache management ──────────────────────────────────────────────────────
/// Invalidate a specific entry.
bool Invalidate(const ShaderKey& key);

/// Invalidate all entries older than the given SPIR-V hash (e.g. after driver update).
uint32_t InvalidateAll();

/// Save the in-memory cache to disk.
bool SaveToDisk(const std::filesystem::path& path = "");

/// Load a previously saved cache from disk.
bool LoadFromDisk(const std::filesystem::path& path = "");

/// Remove entries not used in the last `maxAgeUs` microseconds.
uint32_t Evict(uint64_t maxAgeUs);

// ── Statistics ────────────────────────────────────────────────────────────
CacheStats GetStats();
void       DumpStats();

} // namespace PS5x::ShaderCache
