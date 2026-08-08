// PS5x – Module Registry implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/ModuleRegistry/ModuleRegistry.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Loader/Loader.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace PS5x::ModuleRegistry {

namespace {

using Clock = std::chrono::steady_clock;

static uint64_t NowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
}

struct RegistryState
{
    std::unordered_map<ModuleId, ModuleDesc> modules;
    std::unordered_map<std::string, ModuleId> byName;
    std::vector<ModuleId>                    loadOrder;
    std::atomic<ModuleId>                    nextId{1};
    ModuleId                                 mainModule = INVALID_MODULE;
    mutable std::mutex                       mtx;

    static RegistryState& Get() { static RegistryState s; return s; }
};

// Topological sort for load order
[[maybe_unused]] static std::vector<ModuleId> TopoSort(const RegistryState& rs)
{
    std::unordered_map<ModuleId, int>              inDegree;
    std::unordered_map<ModuleId, std::vector<ModuleId>> adj;

    for (auto& [id, m] : rs.modules) {
        if (!inDegree.count(id)) inDegree[id] = 0;
        for (auto dep : m.deps) {
            adj[dep].push_back(id);
            inDegree[id]++;
        }
    }

    std::vector<ModuleId> order;
    std::vector<ModuleId> q;
    for (auto& [id, deg] : inDegree)
        if (deg == 0) q.push_back(id);

    while (!q.empty()) {
        auto cur = q.back(); q.pop_back();
        order.push_back(cur);
        for (auto nxt : adj[cur])
            if (--inDegree[nxt] == 0) q.push_back(nxt);
    }
    return order;
}

} // anonymous namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init()
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    rs.modules.clear();
    rs.byName.clear();
    rs.loadOrder.clear();
    rs.nextId.store(1);
    rs.mainModule = INVALID_MODULE;
    PS5X_INFO("[ModReg] Initialised.");
    return true;
}

void Shutdown()
{
    Reset();
    PS5X_INFO("[ModReg] Shutdown.");
}

void Reset()
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);

    // Unload in reverse load order
    auto order = rs.loadOrder;
    std::reverse(order.begin(), order.end());
    for (auto id : order) {
        auto it = rs.modules.find(id);
        if (it == rs.modules.end()) continue;
        auto& m = it->second;
        if (m.loaded) {
            Loader::ExecutableInfo copy = m.elfInfo;
            Loader::UnloadExecutable(copy);
        }
    }
    rs.modules.clear();
    rs.byName.clear();
    rs.loadOrder.clear();
    rs.mainModule = INVALID_MODULE;
    PS5X_INFO("[ModReg] Reset.");
}

// ── Registration ─────────────────────────────────────────────────────────
ModuleId Register(const std::string& name,
                  const std::filesystem::path& path,
                  Loader::ExecutableInfo elfInfo,
                  bool isMain)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);

    // Check if already registered
    auto byNameIt = rs.byName.find(name);
    if (byNameIt != rs.byName.end()) {
        auto& existing = rs.modules[byNameIt->second];
        existing.refCount++;
        PS5X_DEBUG("[ModReg] '%s' already registered (refCount=%u)",
                   name.c_str(), existing.refCount);
        return byNameIt->second;
    }

    ModuleId id = rs.nextId.fetch_add(1);

    ModuleDesc desc;
    desc.id          = id;
    desc.name        = name;
    desc.path        = path;
    desc.elfInfo     = std::move(elfInfo);
    desc.isMain      = isMain;
    desc.loaded      = true;
    desc.refCount    = 1;
    desc.loadTimeUs  = NowUs();

    // Extract exports from ELF symbol table
    for (const auto& sym : desc.elfInfo.symbols) {
        if (sym.type == 2 /*STT_FUNC*/ || sym.type == 1 /*STT_OBJECT*/) {
            if (!sym.name.empty() && sym.value != 0) {
                ExportedSymbol es;
                es.name    = sym.name;
                es.address = sym.value + desc.elfInfo.imageBase;
                es.size    = sym.size;
                es.type    = sym.type;
                es.owner   = id;
                desc.exports.push_back(std::move(es));
            }
        }
    }

    rs.modules[id] = std::move(desc);
    rs.byName[name] = id;
    rs.loadOrder.push_back(id);

    if (isMain) rs.mainModule = id;

    PS5X_INFO("[ModReg] Registered '%s' id=%u exports=%zu",
              name.c_str(), id, rs.modules[id].exports.size());
    return id;
}

ModuleId Load(const std::filesystem::path& path)
{
    // Check if already loaded
    auto& rs = RegistryState::Get();
    {
        std::lock_guard lk(rs.mtx);
        auto name = path.filename().string();
        auto it = rs.byName.find(name);
        if (it != rs.byName.end()) {
            rs.modules[it->second].refCount++;
            PS5X_DEBUG("[ModReg] '%s' already loaded, refCount++", name.c_str());
            return it->second;
        }
    }

    Loader::ExecutableInfo elfInfo;
    auto r = Loader::LoadExecutable(path, elfInfo);
    if (r != Loader::LoadResult::Ok) {
        PS5X_ERROR("[ModReg] Load '%s' failed: %s",
                   path.string().c_str(), Loader::LoadResultStr(r));
        return INVALID_MODULE;
    }

    ModuleId id = Register(path.filename().string(), path, std::move(elfInfo));

    // Resolve symbols against existing registry
    if (id != INVALID_MODULE) ApplyRelocations(id);

    return id;
}

bool Unload(ModuleId id)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);

    auto it = rs.modules.find(id);
    if (it == rs.modules.end()) return false;

    auto& m = it->second;
    if (m.refCount > 1) {
        m.refCount--;
        PS5X_DEBUG("[ModReg] Unload '%s' refCount→%u", m.name.c_str(), m.refCount);
        return true;
    }

    PS5X_INFO("[ModReg] Unloading '%s' id=%u", m.name.c_str(), id);

    // Check no other modules depend on this one
    for (auto& [oid, om] : rs.modules) {
        if (oid == id) continue;
        for (auto dep : om.deps)
            if (dep == id) {
                PS5X_WARN("[ModReg] Cannot unload '%s' – '%s' depends on it",
                          m.name.c_str(), om.name.c_str());
                return false;
            }
    }

    if (m.loaded) {
        Loader::UnloadExecutable(m.elfInfo);
        m.loaded = false;
    }

    rs.byName.erase(m.name);
    auto& ord = rs.loadOrder;
    ord.erase(std::remove(ord.begin(), ord.end(), id), ord.end());
    rs.modules.erase(it);
    return true;
}

bool Retain(ModuleId id)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    auto it = rs.modules.find(id);
    if (it == rs.modules.end()) return false;
    it->second.refCount++;
    return true;
}

// ── Symbol resolution ─────────────────────────────────────────────────────
std::optional<ExportedSymbol> ResolveSymbol(const std::string& name)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);

    // Search in load order
    for (auto id : rs.loadOrder) {
        auto it = rs.modules.find(id);
        if (it == rs.modules.end()) continue;
        for (const auto& sym : it->second.exports)
            if (sym.name == name) return sym;
    }
    return std::nullopt;
}

std::optional<ExportedSymbol> ResolveSymbolIn(ModuleId id, const std::string& name)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    auto it = rs.modules.find(id);
    if (it == rs.modules.end()) return std::nullopt;
    for (const auto& sym : it->second.exports)
        if (sym.name == name) return sym;
    return std::nullopt;
}

bool ApplyRelocations(ModuleId id)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    auto it = rs.modules.find(id);
    if (it == rs.modules.end()) return false;

    const auto& m = it->second;
    uint64_t base = m.baseAddr;

    // Process ELF x86-64 relocations.
    // Supported relocation types (System V AMD64 ABI):
    //   R_X86_64_NONE        (0)  — no-op
    //   R_X86_64_64          (1)  — S + A
    //   R_X86_64_PC32        (2)  — S + A - P
    //   R_X86_64_GLOB_DAT    (6)  — GOT entry: S
    //   R_X86_64_JUMP_SLOT   (7)  — PLT entry: S
    //   R_X86_64_RELATIVE    (8)  — B + A
    //   R_X86_64_32          (10) — S + A (32-bit)
    //   R_X86_64_32S         (11) — S + A (32-bit signed)
    struct Elf64Rela { uint64_t offset; uint64_t info; int64_t addend; };
    uint32_t processed = 0, skipped = 0;

    for (auto& rela_section : m.elfInfo.relaSections) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(
                                  base + rela_section.offset);
        size_t count = rela_section.size / sizeof(Elf64Rela);
        for (size_t i = 0; i < count; ++i) {
            Elf64Rela r{};
            std::memcpy(&r, data + i * sizeof(Elf64Rela), sizeof(Elf64Rela));

            uint32_t type  = static_cast<uint32_t>(r.info & 0xFFFFFFFF);
            uint32_t symId = static_cast<uint32_t>(r.info >> 32);

            uint64_t P = base + r.offset;   // relocation site (patch address)
            uint64_t A = static_cast<uint64_t>(r.addend);

            // Resolve symbol value S
            uint64_t S = 0;
            if (symId < m.elfInfo.symbols.size()) {
                const auto& sym = m.elfInfo.symbols[symId];
                // If symbol is defined locally, S = base + sym.value
                if (sym.shndx != 0) {
                    S = base + sym.value;
                } else {
                    // External: look up in other loaded modules
                    for (auto& [oid, om] : rs.modules) {
                        if (oid == id) continue;
                        for (auto& osym : om.elfInfo.symbols) {
                            if (osym.name == sym.name && osym.shndx != 0) {
                                S = om.baseAddr + osym.value;
                                break;
                            }
                        }
                        if (S) break;
                    }
                    if (!S) {
                        PS5X_WARN("[ModReg] Unresolved symbol '%s' in '%s'",
                                  sym.name.c_str(), m.name.c_str());
                        ++skipped;
                        continue;
                    }
                }
            }

            uint64_t B = base;  // module base

            // Apply the relocation by patching guest memory at address P
            auto patch64 = [&](uint64_t val) {
                std::memcpy(reinterpret_cast<void*>(P), &val, 8);
            };
            auto patch32 = [&](uint32_t val) {
                std::memcpy(reinterpret_cast<void*>(P), &val, 4);
            };

            switch (type) {
                case 0:  /* R_X86_64_NONE */      break;
                case 1:  /* R_X86_64_64 */        patch64(S + A); break;
                case 2:  /* R_X86_64_PC32 */      patch32(static_cast<uint32_t>(S + A - P)); break;
                case 6:  /* R_X86_64_GLOB_DAT */  patch64(S); break;
                case 7:  /* R_X86_64_JUMP_SLOT */ patch64(S); break;
                case 8:  /* R_X86_64_RELATIVE */  patch64(B + A); break;
                case 10: /* R_X86_64_32 */        patch32(static_cast<uint32_t>(S + A)); break;
                case 11: /* R_X86_64_32S */       patch32(static_cast<uint32_t>(static_cast<int64_t>(S) + r.addend)); break;
                default:
                    PS5X_WARN("[ModReg] Unhandled reloc type %u in '%s'",
                              type, m.name.c_str());
                    ++skipped;
                    continue;
            }
            ++processed;
        }
    }

    // Resolve DT_NEEDED dependencies
    for (const auto& needed : m.elfInfo.needed) {
        if (rs.byName.find(needed) == rs.byName.end())
            PS5X_WARN("[ModReg] '%s' needs '%s' (not loaded)",
                      m.name.c_str(), needed.c_str());
    }

    PS5X_INFO("[ModReg] Relocated '%s': %u applied, %u skipped.",
              m.name.c_str(), processed, skipped);
    return true;
}

// ── Queries ───────────────────────────────────────────────────────────────
std::optional<ModuleDesc> GetModule(ModuleId id)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    auto it = rs.modules.find(id);
    return (it != rs.modules.end()) ? std::make_optional(it->second) : std::nullopt;
}

std::optional<ModuleDesc> GetModuleByName(const std::string& name)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    auto nit = rs.byName.find(name);
    if (nit == rs.byName.end()) return std::nullopt;
    auto it = rs.modules.find(nit->second);
    return (it != rs.modules.end()) ? std::make_optional(it->second) : std::nullopt;
}

std::vector<ModuleDesc> GetAllModules()
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    std::vector<ModuleDesc> out;
    out.reserve(rs.modules.size());
    for (auto id : rs.loadOrder) {
        auto it = rs.modules.find(id);
        if (it != rs.modules.end()) out.push_back(it->second);
    }
    return out;
}

std::vector<ModuleId> GetLoadOrder()
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    return rs.loadOrder;
}

ModuleId GetMainModule()
{
    return RegistryState::Get().mainModule;
}

uint32_t GetModuleCount()
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    return static_cast<uint32_t>(rs.modules.size());
}

std::vector<ModuleId> GetDependencies(ModuleId id)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    auto it = rs.modules.find(id);
    return (it != rs.modules.end()) ? it->second.deps : std::vector<ModuleId>{};
}

std::vector<ModuleId> GetDependents(ModuleId id)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    std::vector<ModuleId> out;
    for (auto& [oid, m] : rs.modules)
        for (auto dep : m.deps)
            if (dep == id) { out.push_back(oid); break; }
    return out;
}

// ── Diagnostics ───────────────────────────────────────────────────────────
void DumpModules()
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    PS5X_INFO("[ModReg] === Module Table (%u modules) ===",
              static_cast<unsigned>(rs.modules.size()));
    for (auto id : rs.loadOrder) {
        auto it = rs.modules.find(id);
        if (it == rs.modules.end()) continue;
        const auto& m = it->second;
        PS5X_INFO("[ModReg]  [%3u] %-30s  refs=%u  exports=%zu  %s",
                  id, m.name.c_str(), m.refCount, m.exports.size(),
                  m.isMain ? "[MAIN]" : "");
    }
}

void DumpSymbols(ModuleId id)
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    auto it = rs.modules.find(id);
    if (it == rs.modules.end()) return;
    const auto& m = it->second;
    PS5X_INFO("[ModReg] Symbols in '%s':", m.name.c_str());
    for (const auto& sym : m.exports)
        PS5X_INFO("[ModReg]   0x%016llx  %s  sz=%zu",
                  static_cast<unsigned long long>(sym.address),
                  sym.name.c_str(), sym.size);
}

void DumpDependencyGraph()
{
    auto& rs = RegistryState::Get();
    std::lock_guard lk(rs.mtx);
    PS5X_INFO("[ModReg] === Dependency Graph ===");
    for (auto id : rs.loadOrder) {
        auto it = rs.modules.find(id);
        if (it == rs.modules.end()) continue;
        const auto& m = it->second;
        if (m.deps.empty()) {
            PS5X_INFO("[ModReg]  %s  (no deps)", m.name.c_str());
        } else {
            for (auto dep : m.deps) {
                auto dit = rs.modules.find(dep);
                std::string dname = (dit != rs.modules.end()) ? dit->second.name : "?";
                PS5X_INFO("[ModReg]  %s  →  %s", m.name.c_str(), dname.c_str());
            }
        }
    }
}

} // namespace PS5x::ModuleRegistry
