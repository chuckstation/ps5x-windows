// PS5x – Loader module (Phase 2 – full ELF64 implementation)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// Implements complete PS5/PS4 ELF64 loading:
//   • ELF64 + SCE program header parsing
//   • PT_LOAD segment mapping via Memory::Map
//   • Dynamic symbol table parsing
//   • Entry point extraction
//   • param.sfo metadata parsing
//   • Firmware path validation (user must supply)
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PS5x::Loader {

// ── Result codes ──────────────────────────────────────────────────────────
enum class LoadResult : int32_t
{
    Ok                  =  0,
    FileNotFound        = -1,
    InvalidElf          = -2,
    UnsupportedArch     = -3,
    MissingSymbol       = -4,
    MemoryError         = -5,
    FirmwareRequired    = -6,
    InvalidProgramHeader= -7,
    RelocationFailed    = -8,
    AlreadyLoaded       = -9,
    NotLoaded           = -10,
    IoError             = -11,
};

const char* LoadResultStr(LoadResult r);

// ── Segment descriptor ────────────────────────────────────────────────────
struct Segment
{
    uint64_t  vaddr    = 0;   ///< virtual address (guest)
    uint64_t  paddr    = 0;   ///< physical / file offset
    uint64_t  filesz   = 0;   ///< size in file
    uint64_t  memsz    = 0;   ///< size in memory (may be > filesz)
    uint32_t  flags    = 0;   ///< PF_R / PF_W / PF_X
    uint32_t  type     = 0;   ///< PT_LOAD etc.
    uint64_t  align    = 0;
    uintptr_t hostBase = 0;   ///< mapped host address
};

// ── Dynamic symbol ────────────────────────────────────────────────────────
struct Symbol
{
    std::string name;
    uint64_t    value    = 0;
    uint64_t    size     = 0;
    uint8_t     binding  = 0;  ///< STB_*
    uint8_t     type     = 0;  ///< STT_*
    uint8_t     visibility = 0;
};

// ── Loaded executable info ────────────────────────────────────────────────
struct ExecutableInfo
{
    std::filesystem::path    path;
    std::string              titleId;
    std::string              appVersion;
    std::string              contentId;
    uint64_t                 imageBase   = 0;   ///< actual load address
    uint64_t                 entryPoint  = 0;   ///< absolute host entry
    uint64_t                 imageSize   = 0;
    std::vector<Segment>     segments;
    std::vector<Symbol>      symbols;
    std::vector<std::string> needed;            ///< DT_NEEDED
    bool                     isPic       = false; ///< position-independent
    bool                     loaded      = false;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
void Init();
void Shutdown();

// ── Core API ─────────────────────────────────────────────────────────────

/// Parse ELF headers only – does not allocate memory.
LoadResult InspectElf(const std::filesystem::path& path, ExecutableInfo& out);

/// Fully load: parse + map segments + apply relocations.
LoadResult LoadExecutable(const std::filesystem::path& path, ExecutableInfo& out);

/// Unmap all segments of a loaded executable.
LoadResult UnloadExecutable(ExecutableInfo& info);

/// Validate that an ELF is a supported PS5 executable format.
LoadResult ValidateExecutable(const ExecutableInfo& info);

/// Return the entry point of the currently loaded executable.
uint64_t GetEntryPoint();

/// Map all PT_LOAD segments into host virtual memory.
LoadResult MapSegments(ExecutableInfo& info,
                       const std::filesystem::path& path);

/// Execute the loaded program (blocks until exit or error).
LoadResult Execute();

/// Reset loader state (unload everything).
void Reset();

// ── Firmware ─────────────────────────────────────────────────────────────
/// PS5x never supplies, bundles, or downloads firmware.
/// Returns Ok if path exists; FirmwareRequired otherwise.
LoadResult ValidateFirmware(const std::filesystem::path& firmwarePath);

/// Parse /param.sfo and populate titleId / appVersion / contentId in info.
LoadResult LoadParamSfo(const std::filesystem::path& sfoPath,
                        ExecutableInfo& info);

} // namespace PS5x::Loader
