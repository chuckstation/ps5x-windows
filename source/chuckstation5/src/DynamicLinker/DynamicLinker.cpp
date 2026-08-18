// ChuckStation5 – Dynamic Linker implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "ChuckStation5/DynamicLinker/DynamicLinker.h"
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/ModuleRegistry/ModuleRegistry.h"
#include "ChuckStation5/RuntimeEvents/RuntimeEvents.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>

namespace ChuckStation5::DynamicLinker {

// ── Name table ────────────────────────────────────────────────────────────
const char* RelocTypeName(RelocType t)
{
    switch (t) {
        case RelocType::None:     return "R_NONE";
        case RelocType::R64:      return "R_X86_64_64";
        case RelocType::PC32:     return "R_X86_64_PC32";
        case RelocType::GlobDat:  return "R_X86_64_GLOB_DAT";
        case RelocType::JumpSlot: return "R_X86_64_JUMP_SLOT";
        case RelocType::Relative: return "R_X86_64_RELATIVE";
        case RelocType::GotPcRel: return "R_X86_64_GOTPCREL";
        case RelocType::Size32:   return "R_X86_64_SIZE32";
        case RelocType::Size64:   return "R_X86_64_SIZE64";
    }
    return "R_UNKNOWN";
}

// ── Linker state ──────────────────────────────────────────────────────────
namespace {

struct LinkerState
{
    // Symbol cache: name → CachedSymbol
    std::unordered_map<std::string, CachedSymbol> cache;
    std::mutex                                     cacheMtx;

    // Per-module relocations
    std::unordered_map<uint32_t, std::vector<RelocEntry>> relocations;
    std::mutex                                             relocMtx;

    std::atomic<uint64_t> cacheHits{0};
    std::atomic<uint64_t> cacheMisses{0};
    std::atomic<uint32_t> totalRelocs{0};
    std::atomic<uint32_t> resolvedRelocs{0};
    std::atomic<uint32_t> unresolvedRelocs{0};
    std::atomic<uint32_t> lazyRelocs{0};

    static LinkerState& Get() { static LinkerState s; return s; }
};

// Apply a 64-bit write to a host address (bypasses page protection temporarily)
bool Patch64(uintptr_t hostAddr, uint64_t value)
{
    if (!Memory::IsWritable(hostAddr, 8)) {
        // Make writable, patch, restore RX
        if (!Memory::Protect(hostAddr, 8, Memory::Prot::RW)) return false;
        *reinterpret_cast<uint64_t*>(hostAddr) = value;
        Memory::Protect(hostAddr, 8, Memory::Prot::RX);
    } else {
        *reinterpret_cast<uint64_t*>(hostAddr) = value;
    }
    return true;
}

bool Patch32(uintptr_t hostAddr, uint32_t value)
{
    if (!Memory::IsWritable(hostAddr, 4)) {
        if (!Memory::Protect(hostAddr, 4, Memory::Prot::RW)) return false;
        *reinterpret_cast<uint32_t*>(hostAddr) = value;
        Memory::Protect(hostAddr, 4, Memory::Prot::RX);
    } else {
        *reinterpret_cast<uint32_t*>(hostAddr) = value;
    }
    return true;
}

} // anonymous namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init()
{
    auto& ls = LinkerState::Get();
    {
        std::lock_guard lc(ls.cacheMtx);
        ls.cache.clear();
    }
    {
        std::lock_guard lr(ls.relocMtx);
        ls.relocations.clear();
    }
    ls.cacheHits.store(0); ls.cacheMisses.store(0);
    ls.totalRelocs.store(0); ls.resolvedRelocs.store(0);
    ls.unresolvedRelocs.store(0); ls.lazyRelocs.store(0);
    CHUCKSTATION5_INFO("[DynLink] Initialised.");
    return true;
}

void Shutdown() { Reset(); CHUCKSTATION5_INFO("[DynLink] Shutdown."); }

void Reset()
{
    auto& ls = LinkerState::Get();
    std::lock_guard lc(ls.cacheMtx);
    std::lock_guard lr(ls.relocMtx);
    ls.cache.clear();
    ls.relocations.clear();
}

// ── Symbol lookup ─────────────────────────────────────────────────────────
std::optional<CachedSymbol> LookupSymbol(const std::string& name)
{
    auto& ls = LinkerState::Get();
    {
        std::lock_guard lk(ls.cacheMtx);
        auto it = ls.cache.find(name);
        if (it != ls.cache.end()) {
            it->second.hitCount++;
            ls.cacheHits.fetch_add(1, std::memory_order_relaxed);
            return it->second;
        }
    }
    ls.cacheMisses.fetch_add(1, std::memory_order_relaxed);

    // Search ModuleRegistry
    auto sym = ModuleRegistry::ResolveSymbol(name);
    if (!sym) {
        CHUCKSTATION5_DEBUG("[DynLink] Unresolved symbol: %s", name.c_str());
        RuntimeEvents::Publish(RuntimeEvents::EventType::SymbolMissing,
            RuntimeEvents::SymbolPayload{name, 0, ModuleRegistry::INVALID_MODULE});
        return std::nullopt;
    }

    CachedSymbol cs;
    cs.name     = sym->name;
    cs.address  = sym->address;
    cs.moduleId = sym->owner;
    cs.hitCount = 1;

    {
        std::lock_guard lk(ls.cacheMtx);
        ls.cache[name] = cs;
    }

    RuntimeEvents::Publish(RuntimeEvents::EventType::SymbolResolved,
        RuntimeEvents::SymbolPayload{name, sym->address, sym->owner});
    return cs;
}

std::optional<CachedSymbol> LookupSymbolIn(ModuleRegistry::ModuleId id,
                                            const std::string& name)
{
    auto sym = ModuleRegistry::ResolveSymbolIn(id, name);
    if (!sym) return std::nullopt;

    CachedSymbol cs;
    cs.name     = sym->name;
    cs.address  = sym->address;
    cs.moduleId = sym->owner;
    cs.hitCount = 1;
    return cs;
}

void InvalidateCache(ModuleRegistry::ModuleId id)
{
    auto& ls = LinkerState::Get();
    std::lock_guard lk(ls.cacheMtx);
    for (auto it = ls.cache.begin(); it != ls.cache.end(); ) {
        if (it->second.moduleId == id) it = ls.cache.erase(it);
        else ++it;
    }
    CHUCKSTATION5_DEBUG("[DynLink] Cache invalidated for module %u", id);
}

void ClearCache()
{
    auto& ls = LinkerState::Get();
    std::lock_guard lk(ls.cacheMtx);
    ls.cache.clear();
}

// ── Relocation application ────────────────────────────────────────────────
bool ApplyReloc(const RelocEntry& reloc)
{
    if (reloc.type == RelocType::None) return true;
    if (reloc.lazy) return true;  // deferred until first call

    uintptr_t site = static_cast<uintptr_t>(reloc.offset);

    if (reloc.type == RelocType::Relative) {
        // R_X86_64_RELATIVE: patch64(B + A)
        // Resolve B (module base) from ModuleRegistry by the owning module id.
        uint64_t B = 0;
        if (reloc.symModuleId != 0) {
            auto mods = ModuleRegistry::GetAll();
            for (auto& m : mods) {
                if (m.id == reloc.symModuleId) { B = m.baseAddr; break; }
            }
        }
        uint64_t val = B + static_cast<uint64_t>(reloc.addend);
        return Patch64(site, val);
    }

    // All other relocs need a symbol
    if (reloc.symbolName.empty()) return false;
    auto sym = LookupSymbol(reloc.symbolName);
    if (!sym) return false;

    uint64_t S = sym->address;
    int64_t  A = reloc.addend;
    uintptr_t P = site;

    switch (reloc.type) {
        case RelocType::R64:
            return Patch64(site, static_cast<uint64_t>(S + A));
        case RelocType::PC32: {
            int64_t val = static_cast<int64_t>(S) + A - static_cast<int64_t>(P);
            return Patch32(site, static_cast<uint32_t>(static_cast<int32_t>(val)));
        }
        case RelocType::GlobDat:
        case RelocType::JumpSlot:
            return Patch64(site, S);
        default:
            CHUCKSTATION5_WARN("[DynLink] Unsupported reloc type %s for %s",
                      RelocTypeName(reloc.type), reloc.symbolName.c_str());
            return false;
    }
}

// ── Module linking ────────────────────────────────────────────────────────
LinkResult LinkModule(ModuleRegistry::ModuleId id)
{
    LinkResult result;
    auto& ls = LinkerState::Get();
    std::lock_guard lr(ls.relocMtx);
    auto it = ls.relocations.find(id);
    if (it == ls.relocations.end()) {
        CHUCKSTATION5_DEBUG("[DynLink] LinkModule %u: no relocations registered", id);
        return result;
    }

    for (auto& reloc : it->second) {
        ls.totalRelocs.fetch_add(1, std::memory_order_relaxed);
        if (reloc.lazy) {
            result.lazy++;
            ls.lazyRelocs.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (ApplyReloc(reloc)) {
            result.resolved++;
            reloc.resolved = true;
            ls.resolvedRelocs.fetch_add(1, std::memory_order_relaxed);
        } else {
            result.unresolved++;
            ls.unresolvedRelocs.fetch_add(1, std::memory_order_relaxed);
            if (!reloc.symbolName.empty())
                result.missingSymbols.push_back(reloc.symbolName);
        }
    }

    CHUCKSTATION5_INFO("[DynLink] LinkModule %u: resolved=%u unresolved=%u lazy=%u",
              id, result.resolved, result.unresolved, result.lazy);
    return result;
}

LinkResult LinkAll()
{
    LinkResult total;
    auto order = ModuleRegistry::GetLoadOrder();
    for (auto id : order) {
        auto r = LinkModule(id);
        total.resolved   += r.resolved;
        total.unresolved += r.unresolved;
        total.lazy       += r.lazy;
        for (auto& s : r.missingSymbols)
            total.missingSymbols.push_back(s);
    }
    CHUCKSTATION5_INFO("[DynLink] LinkAll: resolved=%u unresolved=%u lazy=%u missing=%zu",
              total.resolved, total.unresolved, total.lazy,
              total.missingSymbols.size());
    return total;
}

uint32_t EagerBind(ModuleRegistry::ModuleId id)
{
    auto& ls = LinkerState::Get();
    std::lock_guard lr(ls.relocMtx);
    auto it = ls.relocations.find(id);
    if (it == ls.relocations.end()) return 0;
    uint32_t count = 0;
    for (auto& reloc : it->second) {
        if (!reloc.lazy || reloc.resolved) continue;
        reloc.lazy = false;
        if (ApplyReloc(reloc)) { reloc.resolved = true; count++; }
    }
    CHUCKSTATION5_INFO("[DynLink] EagerBind %u: bound %u PLT entries", id, count);
    return count;
}

uint32_t LazyCount()
{
    auto& ls = LinkerState::Get();
    std::lock_guard lr(ls.relocMtx);
    uint32_t n = 0;
    for (auto& [id, relocs] : ls.relocations)
        for (auto& r : relocs)
            if (r.lazy && !r.resolved) n++;
    return n;
}

// ── Dependency graph ──────────────────────────────────────────────────────
std::optional<std::vector<ModuleRegistry::ModuleId>> DetectCycles()
{
    auto modules = ModuleRegistry::GetAllModules();

    // DFS cycle detection
    std::set<ModuleRegistry::ModuleId> visited, inStack;
    std::vector<ModuleRegistry::ModuleId> path;

    std::function<bool(ModuleRegistry::ModuleId)> dfs;
    dfs = [&](ModuleRegistry::ModuleId id) -> bool {
        visited.insert(id);
        inStack.insert(id);
        path.push_back(id);

        for (auto dep : ModuleRegistry::GetDependencies(id)) {
            if (!visited.count(dep) && dfs(dep)) return true;
            if (inStack.count(dep)) {
                path.push_back(dep); // mark cycle endpoint
                return true;
            }
        }
        inStack.erase(id);
        path.pop_back();
        return false;
    };

    for (const auto& m : modules) {
        if (!visited.count(m.id)) {
            path.clear();
            if (dfs(m.id)) {
                CHUCKSTATION5_WARN("[DynLink] Circular dependency detected (len=%zu)", path.size());
                return path;
            }
        }
    }
    return std::nullopt;
}

std::vector<ModuleRegistry::ModuleId> TopologicalOrder()
{
    // Kahn's algorithm over ModuleRegistry deps
    auto modules = ModuleRegistry::GetAllModules();
    std::unordered_map<uint32_t, int> inDeg;
    std::unordered_map<uint32_t, std::vector<uint32_t>> adj;

    for (const auto& m : modules) {
        if (!inDeg.count(m.id)) inDeg[m.id] = 0;
        for (auto dep : m.deps) {
            adj[dep].push_back(m.id);
            inDeg[m.id]++;
        }
    }

    std::vector<ModuleRegistry::ModuleId> order;
    std::vector<uint32_t> q;
    for (auto& [id, deg] : inDeg)
        if (deg == 0) q.push_back(id);

    while (!q.empty()) {
        auto cur = q.back(); q.pop_back();
        order.push_back(cur);
        for (auto nxt : adj[cur])
            if (--inDeg[nxt] == 0) q.push_back(nxt);
    }
    return order;
}

bool CanUnload(ModuleRegistry::ModuleId id)
{
    auto deps = ModuleRegistry::GetDependents(id);
    if (!deps.empty()) {
        CHUCKSTATION5_WARN("[DynLink] CanUnload %u: %zu module(s) still depend on it", id, deps.size());
        return false;
    }
    return true;
}

// ── Diagnostics ───────────────────────────────────────────────────────────
void DumpRelocations(ModuleRegistry::ModuleId id)
{
    auto& ls = LinkerState::Get();
    std::lock_guard lr(ls.relocMtx);
    auto it = ls.relocations.find(id);
    if (it == ls.relocations.end()) {
        CHUCKSTATION5_INFO("[DynLink] No relocations for module %u", id);
        return;
    }
    CHUCKSTATION5_INFO("[DynLink] Relocations for module %u (%zu):", id, it->second.size());
    for (const auto& r : it->second) {
        CHUCKSTATION5_INFO("[DynLink]   0x%llx %-20s %-30s addend=%lld %s%s",
                  static_cast<unsigned long long>(r.offset),
                  RelocTypeName(r.type),
                  r.symbolName.c_str(),
                  static_cast<long long>(r.addend),
                  r.resolved ? "[OK]" : "[?]",
                  r.lazy     ? "[lazy]" : "");
    }
}

void DumpSymbolCache()
{
    auto& ls = LinkerState::Get();
    std::lock_guard lk(ls.cacheMtx);
    CHUCKSTATION5_INFO("[DynLink] Symbol cache (%zu entries):", ls.cache.size());
    for (const auto& [name, sym] : ls.cache)
        CHUCKSTATION5_INFO("[DynLink]   %-40s 0x%llx mod=%u hits=%llu",
                  name.c_str(),
                  static_cast<unsigned long long>(sym.address),
                  sym.moduleId,
                  static_cast<unsigned long long>(sym.hitCount));
}

void DumpLinkResult(const LinkResult& r)
{
    CHUCKSTATION5_INFO("[DynLink] Link result: resolved=%u unresolved=%u lazy=%u",
              r.resolved, r.unresolved, r.lazy);
    for (const auto& s : r.missingSymbols)
        CHUCKSTATION5_WARN("[DynLink]   missing: %s", s.c_str());
}

// ── Statistics ────────────────────────────────────────────────────────────
LinkerStats GetStats()
{
    auto& ls = LinkerState::Get();
    LinkerStats s;
    s.cacheHits        = ls.cacheHits.load();
    s.cacheMisses      = ls.cacheMisses.load();
    s.totalRelocations = ls.totalRelocs.load();
    s.resolvedRelocs   = ls.resolvedRelocs.load();
    s.unresolvedRelocs = ls.unresolvedRelocs.load();
    s.lazyRelocs       = ls.lazyRelocs.load();
    std::lock_guard lk(ls.cacheMtx);
    s.cacheEntries     = static_cast<uint32_t>(ls.cache.size());
    return s;
}

} // namespace ChuckStation5::DynamicLinker
