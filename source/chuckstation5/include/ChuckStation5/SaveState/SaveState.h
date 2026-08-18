// ChuckStation5 – Save State Manager
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Provides snapshot/restore of the full emulator state:
//   • CPU context (GPRs, RIP, RFLAGS, XMM)
//   • Memory regions (committed pages with their data)
//   • GPU fence/queue state
// Save states are stored as binary files with a typed header,
// per-subsystem blobs, and a CRC32 checksum for integrity.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ChuckStation5::Cpu { struct CpuContext; }

namespace ChuckStation5::SaveState {

// ── On-disk header ────────────────────────────────────────────────────────
struct SaveStateHeader {
    char     magic[8]       = {'P','S','5','x','S','S','\0','\0'};
    uint32_t version        = 1;
    uint64_t timestamp      = 0;        // Unix epoch ms
    char     gameName[128]  = {};
    char     description[256] = {};
    uint32_t cpuStateSize   = 0;
    uint32_t memoryMapSize  = 0;
    uint32_t gpuStateSize   = 0;
    uint32_t checksum       = 0;        // CRC32 of payload
};

// ── Result codes ──────────────────────────────────────────────────────────
enum class SaveResult : uint8_t {
    Ok = 0,
    FileError,
    InvalidState,
    DiskFull,
    ChecksumMismatch,
};

enum class LoadResult : uint8_t {
    Ok = 0,
    FileNotFound,
    CorruptFile,
    VersionMismatch,
    ChecksumMismatch,
    MemoryConflict,
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(const std::filesystem::path& saveDir = "savestates");
void Shutdown();

// ── Save / Load ───────────────────────────────────────────────────────────
/// Save current emulator state to the given slot (0-9).
SaveResult Save(int slot, const std::string& description = "");

/// Load emulator state from the given slot.
LoadResult Load(int slot);

/// Delete a save state file for the given slot.
bool Delete(int slot);

// ── Query ─────────────────────────────────────────────────────────────────
/// Returns true if a save state exists for the given slot.
bool HasSave(int slot);

/// Returns a sorted list of all occupied slot indices.
std::vector<int> ListSaves();

/// Read the header of a save state without loading it.
SaveStateHeader GetHeader(int slot);

/// Get the save directory path.
std::filesystem::path GetSaveDirectory();

} // namespace ChuckStation5::SaveState
