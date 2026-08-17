// PS5x – Filesystem module (Phase 2 – expanded VFS)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// PS5 VFS mount layout:
//   /app0/      – game content (read-only)
//   /savedata/  – save data (read/write)
//   /system/    – firmware modules (user-supplied, read-only)
//   /temp/      – scratch space (ephemeral)
//   /user/      – user profile data (read/write)
//   /hostapp/   – host-side test content (debug only)
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace PS5x::Filesystem
{

// ── Mount points ──────────────────────────────────────────────────────────
enum class MountPoint : uint8_t
{
	App0 = 0,     ///< /app0/
	SaveData = 1, ///< /savedata/
	System = 2,   ///< /system/   (user-supplied firmware)
	Temp = 3,     ///< /temp/
	User = 4,     ///< /user/
	HostApp = 5,  ///< /hostapp/  (debug host bridge)
	COUNT = 6,
};

// ── Open flags ────────────────────────────────────────────────────────────
enum class OpenFlags : uint32_t
{
	Read = 1 << 0,
	Write = 1 << 1,
	Create = 1 << 2,
	Truncate = 1 << 3,
	Append = 1 << 4,
	NonBlock = 1 << 5,
};
inline OpenFlags operator|(OpenFlags a, OpenFlags b)
{
	return static_cast<OpenFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool operator&(OpenFlags a, OpenFlags b)
{
	return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// ── Seek origin ───────────────────────────────────────────────────────────
enum class SeekOrigin : int
{
	Set = 0,
	Cur = 1,
	End = 2
};

// ── File handle ───────────────────────────────────────────────────────────
using FileHandle = int32_t;
static constexpr FileHandle INVALID_FD = -1;

// ── Directory entry ───────────────────────────────────────────────────────
struct DirEntry
{
	std::string name;
	uint64_t size = 0;
	bool isDir = false;
	bool isLink = false;
	uint64_t modTime = 0; ///< Unix timestamp
};

// ── Stat result ───────────────────────────────────────────────────────────
struct FileStat
{
	uint64_t size = 0;
	uint64_t modTime = 0;
	bool isDir = false;
	bool exists = false;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
void Init();
void Shutdown();

// ── Mount management ──────────────────────────────────────────────────────
bool Mount(MountPoint mp, const std::filesystem::path& hostPath, bool readOnly = false);
bool Unmount(MountPoint mp);
bool IsMounted(MountPoint mp);
std::optional<std::filesystem::path> GetHostPath(MountPoint mp);

// ── Path resolution ───────────────────────────────────────────────────────
/// Translate a PS5 guest path → host path.
/// Guest paths must start with /app0/, /savedata/, etc.
std::optional<std::filesystem::path> Resolve(std::string_view guestPath);

/// Normalise a guest path: collapse .., remove double slashes.
std::string Normalise(std::string_view guestPath);

// ── File I/O ──────────────────────────────────────────────────────────────
FileHandle Open(std::string_view guestPath, OpenFlags flags);
bool Close(FileHandle fd);
int64_t Read(FileHandle fd, void* buf, uint64_t size);
int64_t Write(FileHandle fd, const void* buf, uint64_t size);
int64_t Seek(FileHandle fd, int64_t offset, SeekOrigin origin);
int64_t Tell(FileHandle fd);
int64_t Size(FileHandle fd);
bool Flush(FileHandle fd);

// ── Directory I/O ─────────────────────────────────────────────────────────
std::vector<DirEntry> ReadDir(std::string_view guestPath);
bool Exists(std::string_view guestPath);
bool MakeDir(std::string_view guestPath, bool recursive = true);
bool Remove(std::string_view guestPath);
bool Rename(std::string_view from, std::string_view to);
FileStat Stat(std::string_view guestPath);

// ── Save data helpers ─────────────────────────────────────────────────────
/// Ensure /savedata/<titleId>/ exists and return its guest path.
std::string EnsureSaveDir(std::string_view titleId);

// ── Debug ─────────────────────────────────────────────────────────────────
void DumpMounts(); ///< Log all mount points to the logger.

// ── Phase 6 extensions ────────────────────────────────────────────────────

// ── Filesystem event tracing ──────────────────────────────────────────────

enum class FsEvent : uint8_t
{
	Open = 0,
	Close = 1,
	Read = 2,
	Write = 3,
	Stat = 4,
	MkDir = 5,
	Remove = 6,
	Rename = 7,
	Mount = 8,
};
const char* FsEventName(FsEvent e);

struct FsTraceEntry
{
	uint64_t timestampUs = 0;
	FsEvent event = FsEvent::Open;
	std::string path;
	int64_t bytes = 0; ///< for reads/writes
	bool success = true;
};

void EnableTracing(bool enable);
bool IsTracingEnabled();
std::vector<FsTraceEntry> GetTrace(size_t maxEntries = 1024);
void ClearTrace();

// ── Temp filesystem ───────────────────────────────────────────────────────

/// Create a named temporary file in /temp/.
/// Returns a guest path like /temp/<name>.
std::string CreateTempFile(std::string_view name = "");

/// Return the current size of all open files.
uint64_t GetOpenFileCount();

// ── Read-only enforcement ──────────────────────────────────────────────────

/// Check if a mount is currently read-only.
bool IsReadOnly(MountPoint mp);

} // namespace PS5x::Filesystem
