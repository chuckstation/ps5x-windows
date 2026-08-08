// PS5x – Dynamic Linker
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// Implements ELF x86-64 relocation processing over the Module Registry:
//   • R_X86_64_64 / R_X86_64_PC32 / R_X86_64_GLOB_DAT / R_X86_64_JUMP_SLOT
//   • PLT/GOT patching
//   • Symbol lookup with cache
//   • Lazy binding (PLT trampoline)
//   • Circular dependency detection
//   • Module unloading safety checks
#pragma once

#include "PS5x/ModuleRegistry/ModuleRegistry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace PS5x::DynamicLinker {

// ── Relocation types (x86-64 ELF) ────────────────────────────────────────
enum class RelocType : uint32_t
{
    None         = 0,
    R64          = 1,   ///< R_X86_64_64:        S + A
    PC32         = 2,   ///< R_X86_64_PC32:      S + A - P
    GlobDat      = 6,   ///< R_X86_64_GLOB_DAT:  S
    JumpSlot     = 7,   ///< R_X86_64_JUMP_SLOT: S
    Relative     = 8,   ///< R_X86_64_RELATIVE:  B + A
    GotPcRel     = 9,   ///< R_X86_64_GOTPCREL
    Size32       = 32,
    Size64       = 33,
};

const char* RelocTypeName(RelocType t);

// ── Relocation entry ──────────────────────────────────────────────────────
struct RelocEntry
{
    uint64_t    offset     = 0;   ///< host address of patch site
    RelocType   type       = RelocType::None;
    std::string symbolName;
    int64_t     addend     = 0;
    uint32_t    symModuleId = ModuleRegistry::INVALID_MODULE;
    bool        resolved   = false;
    bool        lazy       = false;   ///< JUMP_SLOT – lazily resolved
};

// ── Symbol cache entry ─────────────────────────────────────────────────────
struct CachedSymbol
{
    std::string   name;
    uint64_t      address   = 0;
    uint32_t      moduleId  = ModuleRegistry::INVALID_MODULE;
    uint64_t      hitCount  = 0;
};

// ── Link result ───────────────────────────────────────────────────────────
struct LinkResult
{
    uint32_t resolved       = 0;
    uint32_t unresolved     = 0;
    uint32_t lazy           = 0;
    std::vector<std::string> missingSymbols;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();
void Reset();

// ── Linking ───────────────────────────────────────────────────────────────

/// Link all registered modules in the ModuleRegistry.
/// Processes relocations and patches the GOT/PLT.
LinkResult LinkAll();

/// Link a single module (must already be in the ModuleRegistry).
LinkResult LinkModule(ModuleRegistry::ModuleId id);

/// Process a single relocation entry.
bool ApplyReloc(const RelocEntry& reloc);

// ── Symbol lookup (with caching) ──────────────────────────────────────────

/// Resolve a symbol by name. Checks the cache first.
std::optional<CachedSymbol> LookupSymbol(const std::string& name);

/// Resolve a symbol in a specific module.
std::optional<CachedSymbol> LookupSymbolIn(ModuleRegistry::ModuleId id,
                                            const std::string& name);

/// Invalidate the symbol cache for a module (call on unload).
void InvalidateCache(ModuleRegistry::ModuleId id);

/// Clear the entire symbol cache.
void ClearCache();

// ── GOT/PLT ──────────────────────────────────────────────────────────────

/// Eagerly resolve all JUMP_SLOT relocations (disable lazy binding).
uint32_t EagerBind(ModuleRegistry::ModuleId id);

/// Return the number of remaining lazy (unresolved) PLT entries.
uint32_t LazyCount();

// ── Dependency graph ──────────────────────────────────────────────────────

/// Check for circular dependencies in the module graph.
/// Returns the cycle path if one is found.
std::optional<std::vector<ModuleRegistry::ModuleId>> DetectCycles();

/// Return a topologically sorted load order for all registered modules.
std::vector<ModuleRegistry::ModuleId> TopologicalOrder();

/// Check if it is safe to unload a module (no remaining dependents in use).
bool CanUnload(ModuleRegistry::ModuleId id);

// ── Diagnostics ───────────────────────────────────────────────────────────
void DumpRelocations(ModuleRegistry::ModuleId id);
void DumpSymbolCache();
void DumpLinkResult(const LinkResult& r);

// ── Statistics ────────────────────────────────────────────────────────────
struct LinkerStats
{
    uint64_t  cacheHits         = 0;
    uint64_t  cacheMisses       = 0;
    uint32_t  totalRelocations  = 0;
    uint32_t  resolvedRelocs    = 0;
    uint32_t  unresolvedRelocs  = 0;
    uint32_t  lazyRelocs        = 0;
    uint32_t  cacheEntries      = 0;
};
LinkerStats GetStats();

} // namespace PS5x::DynamicLinker
