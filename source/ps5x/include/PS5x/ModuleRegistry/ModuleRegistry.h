// PS5x – Module Registry
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// Tracks all loaded modules: dependency graph, symbol table,
// reference counting, and dynamic loading/unloading.
#pragma once

#include "PS5x/Loader/Loader.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace PS5x::ModuleRegistry
{

// ── Module ID ─────────────────────────────────────────────────────────────
using ModuleId = uint32_t;
static constexpr ModuleId INVALID_MODULE = 0;

// ── Symbol visibility ─────────────────────────────────────────────────────
enum class SymbolVis : uint8_t
{
	Default = 0,
	Hidden = 1,
	Protected = 2,
	Internal = 3
};

// ── Exported symbol ───────────────────────────────────────────────────────
struct ExportedSymbol
{
	std::string name;
	uint64_t address = 0; ///< host-virtual address after relocation
	uint64_t size = 0;
	uint8_t type = 0; ///< STT_FUNC / STT_OBJECT
	SymbolVis vis = SymbolVis::Default;
	ModuleId owner = INVALID_MODULE;
};

// ── Module descriptor ─────────────────────────────────────────────────────
struct ModuleDesc
{
	ModuleId id = INVALID_MODULE;
	std::string name;
	std::filesystem::path path;
	Loader::ExecutableInfo elfInfo;
	std::vector<ModuleId> deps; ///< modules this one depends on
	std::vector<ExportedSymbol> exports;
	uint32_t refCount = 0;
	bool isMain = false;
	bool loaded = false;
	uint64_t loadTimeUs = 0;
	uint64_t baseAddr = 0;
	uint64_t size = 0;
};

using Module = ModuleDesc;

// ── Relocation record ─────────────────────────────────────────────────────
struct Relocation
{
	uint64_t offset = 0; ///< host address of relocation site
	uint64_t addend = 0;
	std::string symbolName;
	uint32_t type = 0; ///< R_X86_64_*
	ModuleId module = INVALID_MODULE;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init();
void Shutdown();
void Reset(); ///< Unload all modules and clear registry.

// ── Registration ─────────────────────────────────────────────────────────

/// Register a new module (e.g. the main executable after loading).
ModuleId Register(const std::string& name, const std::filesystem::path& path, Loader::ExecutableInfo elfInfo,
				  bool isMain = false);

ModuleId Register(const ModuleDesc& desc);

/// Dynamically load and register a module by path.
/// Handles symbol resolution and relocation against already-loaded modules.
ModuleId Load(const std::filesystem::path& path);

/// Decrement reference count; unload when it reaches zero.
bool Unload(ModuleId id);

/// Explicitly retain a module (increment refcount).
bool Retain(ModuleId id);

// ── Symbol resolution ─────────────────────────────────────────────────────

/// Find an exported symbol by name across all loaded modules.
std::optional<ExportedSymbol> ResolveSymbol(const std::string& name);

/// Find an exported symbol by name in a specific module.
std::optional<ExportedSymbol> ResolveSymbolIn(ModuleId id, const std::string& name);

/// Apply pending relocations for a module given the current symbol table.
bool ApplyRelocations(ModuleId id);

// ── Queries ───────────────────────────────────────────────────────────────
std::optional<ModuleDesc> GetModule(ModuleId id);
std::optional<ModuleDesc> GetModuleByName(const std::string& name);
std::vector<ModuleDesc> GetAllModules();
std::vector<ModuleDesc> GetAll();     // Compatibility alias for GetAllModules
std::vector<ModuleId> GetLoadOrder(); ///< topological load order
ModuleId GetMainModule();
uint32_t GetModuleCount();

/// Return all dependency IDs for a given module (direct only).
std::vector<ModuleId> GetDependencies(ModuleId id);

/// Return all modules that directly depend on this one.
std::vector<ModuleId> GetDependents(ModuleId id);

// ── Diagnostics ───────────────────────────────────────────────────────────
void DumpModules();            ///< Log full module list
void DumpSymbols(ModuleId id); ///< Log all exports of a module
void DumpDependencyGraph();    ///< Log the dep graph

} // namespace PS5x::ModuleRegistry
