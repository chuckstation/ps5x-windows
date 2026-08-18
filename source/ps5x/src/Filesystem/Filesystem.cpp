// PS5x – Filesystem implementation (Phase 2 – expanded VFS)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/Filesystem/Filesystem.h"
#include "PS5x/Logger/Logger.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>
#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

namespace PS5x::Filesystem {

// ── Mount table ───────────────────────────────────────────────────────────
namespace {

struct MountEntry {
    std::string           prefix;     // e.g. "/app0"
    std::filesystem::path hostPath;
    bool                  valid    = false;
    bool                  readOnly = false;
};

struct OpenFile {
    std::FILE*            fp      = nullptr;
    uint64_t              size    = 0;
    bool                  rdOnly  = false;
};

// MountPoint → guest prefix string
static constexpr std::array<const char*, 6> kMountPrefix = {
    "/app0", "/savedata", "/system", "/temp", "/user", "/hostapp"
};

struct VfsState {
    std::array<MountEntry, 6>              mounts;
    std::unordered_map<FileHandle,OpenFile> files;
    FileHandle                             nextFd = 1;
    std::mutex                             mtx;

    static VfsState& Get() { static VfsState s; return s; }
};

// ── Path normalisation ────────────────────────────────────────────────────
std::string NormaliseImpl(std::string_view path)
{
    // Collapse //, handle .., ensure leading /
    std::vector<std::string> parts;
    std::istringstream iss{std::string(path)};
    std::string seg;
    while (std::getline(iss, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else parts.push_back(seg);
    }
    std::string result = "/";
    for (size_t i = 0; i < parts.size(); ++i) {
        result += parts[i];
        if (i + 1 < parts.size()) result += '/';
    }
    return result;
}

std::optional<std::filesystem::path> ResolveImpl(std::string_view guestRaw,
                                                  const VfsState& st)
{
    std::string guest = NormaliseImpl(guestRaw);
    for (const auto& m : st.mounts) {
        if (!m.valid) continue;
        if (guest == m.prefix ||
            guest.starts_with(m.prefix + "/")) {
            auto rel  = guest.substr(m.prefix.size());
            if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
            auto host = m.hostPath;
            if (!rel.empty()) host /= rel;
            return host;
        }
    }
    return std::nullopt;
}

const char* FlagMode(OpenFlags flags, bool rdOnly)
{
    if (rdOnly)  return "rb";
    bool r = flags & OpenFlags::Read;
    bool w = flags & OpenFlags::Write;
    bool t = flags & OpenFlags::Truncate;
    bool a = flags & OpenFlags::Append;
    if (w && t)  return "wb";
    if (w && a)  return "ab";
    if (w && r)  return "r+b";
    if (w)       return "wb";
    return "rb";
}

} // anonymous namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────
void Init()
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    for (auto& m : st.mounts) m = MountEntry{};
    st.files.clear();
    st.nextFd = 1;
    PS5X_INFO("[FS] Virtual filesystem initialised.");
}

void Shutdown()
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    for (auto& [fd, f] : st.files)
        if (f.fp) std::fclose(f.fp);
    st.files.clear();
    for (auto& m : st.mounts) m = MountEntry{};
    PS5X_INFO("[FS] Shutdown.");
}

// ── Mount management ──────────────────────────────────────────────────────
bool Mount(MountPoint mp, const std::filesystem::path& hostPath, bool readOnly)
{
    auto& st = VfsState::Get();
    auto idx = static_cast<size_t>(mp);
    if (idx >= st.mounts.size()) {
        PS5X_ERROR("[FS] Mount: invalid MountPoint %u", static_cast<unsigned>(mp));
        return false;
    }
    if (!std::filesystem::exists(hostPath)) {
        std::error_code ec;
        std::filesystem::create_directories(hostPath, ec);
        if (ec) {
            PS5X_ERROR("[FS] Mount: host path absent and can't create: %s",
                       hostPath.string().c_str());
            return false;
        }
    }
    std::lock_guard lk(st.mtx);
    st.mounts[idx] = MountEntry{kMountPrefix[idx], hostPath, true, readOnly};
    PS5X_INFO("[FS] Mount %s → %s%s",
              kMountPrefix[idx], hostPath.string().c_str(),
              readOnly ? " [ro]" : "");
    return true;
}

bool Unmount(MountPoint mp)
{
    auto& st = VfsState::Get();
    auto idx = static_cast<size_t>(mp);
    if (idx >= st.mounts.size()) return false;
    std::lock_guard lk(st.mtx);
    st.mounts[idx] = MountEntry{};
    PS5X_INFO("[FS] Unmounted %s", kMountPrefix[idx]);
    return true;
}

bool IsMounted(MountPoint mp)
{
    auto& st = VfsState::Get();
    auto idx = static_cast<size_t>(mp);
    if (idx >= st.mounts.size()) return false;
    std::lock_guard lk(st.mtx);
    return st.mounts[idx].valid;
}

std::optional<std::filesystem::path> GetHostPath(MountPoint mp)
{
    auto& st = VfsState::Get();
    auto idx = static_cast<size_t>(mp);
    if (idx >= st.mounts.size()) return std::nullopt;
    std::lock_guard lk(st.mtx);
    if (!st.mounts[idx].valid) return std::nullopt;
    return st.mounts[idx].hostPath;
}

// ── Path helpers ──────────────────────────────────────────────────────────
std::string Normalise(std::string_view guestPath)
{ return NormaliseImpl(guestPath); }

std::optional<std::filesystem::path> Resolve(std::string_view guestPath)
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    auto r = ResolveImpl(guestPath, st);
    if (!r) PS5X_WARN("[FS] Unresolvable: %.*s",
                      static_cast<int>(guestPath.size()), guestPath.data());
    return r;
}

// ── File I/O ──────────────────────────────────────────────────────────────
FileHandle Open(std::string_view guestPath, OpenFlags flags)
{
    auto& st = VfsState::Get();
    std::optional<std::filesystem::path> hostOpt;
    bool rdOnly = false;
    {
        std::lock_guard lk(st.mtx);
        hostOpt = ResolveImpl(guestPath, st);
        // Determine read-only status of mount
        std::string norm = NormaliseImpl(guestPath);
        for (const auto& m : st.mounts)
            if (m.valid && norm.starts_with(m.prefix))
                { rdOnly = m.readOnly; break; }
    }
    if (!hostOpt) return INVALID_FD;

    // Enforce mount read-only
    if (rdOnly && (flags & OpenFlags::Write)) {
        PS5X_ERROR("[FS] Open: write denied on read-only mount: %.*s",
                   static_cast<int>(guestPath.size()), guestPath.data());
        return INVALID_FD;
    }

    const char* mode = FlagMode(flags, rdOnly);

    // Create parent directories if needed
    if (flags & OpenFlags::Create) {
        std::error_code ec;
        std::filesystem::create_directories(hostOpt->parent_path(), ec);
    }

    std::FILE* fp = std::fopen(hostOpt->string().c_str(), mode);
    if (!fp) {
        PS5X_ERROR("[FS] Open failed: %s (%s)", hostOpt->string().c_str(),
                   std::strerror(errno));
        return INVALID_FD;
    }

    std::fseek(fp, 0, SEEK_END);
    auto sz = static_cast<uint64_t>(std::ftell(fp));
    std::fseek(fp, 0, SEEK_SET);

    std::lock_guard lk(st.mtx);
    FileHandle fd = st.nextFd++;
    st.files[fd] = OpenFile{fp, sz, rdOnly};
    return fd;
}

bool Close(FileHandle fd)
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.files.find(fd);
    if (it == st.files.end()) return false;
    std::fclose(it->second.fp);
    st.files.erase(it);
    return true;
}

int64_t Read(FileHandle fd, void* buf, uint64_t size)
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.files.find(fd);
    if (it == st.files.end()) return -1;
    return static_cast<int64_t>(
        std::fread(buf, 1, static_cast<size_t>(size), it->second.fp));
}

int64_t Write(FileHandle fd, const void* buf, uint64_t size)
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.files.find(fd);
    if (it == st.files.end()) return -1;
    if (it->second.rdOnly) return -1;
    return static_cast<int64_t>(
        std::fwrite(buf, 1, static_cast<size_t>(size), it->second.fp));
}

int64_t Seek(FileHandle fd, int64_t offset, SeekOrigin origin)
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.files.find(fd);
    if (it == st.files.end()) return -1;
    int w = (origin == SeekOrigin::Set) ? SEEK_SET :
            (origin == SeekOrigin::Cur) ? SEEK_CUR : SEEK_END;
    std::fseek(it->second.fp, static_cast<long>(offset), w);
    return static_cast<int64_t>(std::ftell(it->second.fp));
}

int64_t Tell(FileHandle fd)
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.files.find(fd);
    if (it == st.files.end()) return -1;
    return static_cast<int64_t>(std::ftell(it->second.fp));
}

int64_t Size(FileHandle fd)
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.files.find(fd);
    if (it == st.files.end()) return -1;
    return static_cast<int64_t>(it->second.size);
}

bool Flush(FileHandle fd)
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    auto it = st.files.find(fd);
    if (it == st.files.end()) return false;
    return std::fflush(it->second.fp) == 0;
}

// ── Directory I/O ─────────────────────────────────────────────────────────
std::vector<DirEntry> ReadDir(std::string_view guestPath)
{
    std::vector<DirEntry> out;
    auto hostOpt = Resolve(guestPath);
    if (!hostOpt) return out;

    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(*hostOpt, ec)) {
        DirEntry e;
        e.name   = de.path().filename().string();
        e.isDir  = de.is_directory(ec);
        e.isLink = de.is_symlink(ec);
        e.size   = e.isDir ? 0 : de.file_size(ec);
        // modTime
        auto wt = de.last_write_time(ec);
        if (!ec) {
            auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(wt);
            e.modTime = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    sctp.time_since_epoch()).count());
        }
        out.push_back(std::move(e));
    }
    return out;
}

bool Exists(std::string_view guestPath)
{
    auto h = Resolve(guestPath);
    return h && std::filesystem::exists(*h);
}

bool MakeDir(std::string_view guestPath, bool recursive)
{
    auto h = Resolve(guestPath);
    if (!h) return false;
    std::error_code ec;
    return recursive ? std::filesystem::create_directories(*h, ec)
                     : std::filesystem::create_directory(*h, ec);
}

bool Remove(std::string_view guestPath)
{
    auto h = Resolve(guestPath);
    if (!h) return false;
    std::error_code ec;
    return std::filesystem::remove(*h, ec);
}

bool Rename(std::string_view from, std::string_view to)
{
    auto hf = Resolve(from);
    auto ht = Resolve(to);
    if (!hf || !ht) return false;
    std::error_code ec;
    std::filesystem::rename(*hf, *ht, ec);
    return !ec;
}

FileStat Stat(std::string_view guestPath)
{
    FileStat s{};
    auto h = Resolve(guestPath);
    if (!h) return s;
    std::error_code ec;
    s.exists = std::filesystem::exists(*h, ec);
    if (!s.exists) return s;
    s.isDir  = std::filesystem::is_directory(*h, ec);
    s.size   = s.isDir ? 0 : std::filesystem::file_size(*h, ec);
    auto wt  = std::filesystem::last_write_time(*h, ec);
    if (!ec) {
        auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(wt);
        s.modTime = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                sctp.time_since_epoch()).count());
    }
    return s;
}

// ── Save data helpers ─────────────────────────────────────────────────────
std::string EnsureSaveDir(std::string_view titleId)
{
    std::string guest = "/savedata/";
    guest += titleId;
    MakeDir(guest, true);
    return guest;
}

// ── Debug ─────────────────────────────────────────────────────────────────
void DumpMounts()
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    PS5X_INFO("[FS] === Mount table ===");
    for (size_t i = 0; i < st.mounts.size(); ++i) {
        const auto& m = st.mounts[i];
        if (m.valid)
            PS5X_INFO("[FS]  %-12s → %s%s",
                      kMountPrefix[i], m.hostPath.string().c_str(),
                      m.readOnly ? " [ro]" : "");
        else
            PS5X_INFO("[FS]  %-12s (unmounted)", kMountPrefix[i]);
    }
}



// ── Phase 6 implementations ───────────────────────────────────────────────

namespace {

struct FsTraceState {
    std::atomic<bool>    enabled{false};
    std::deque<FsTraceEntry> entries;
    std::mutex           mtx;
    static constexpr size_t MAX = 65536;

    static FsTraceState& Get() { static FsTraceState s; return s; }
};

void TraceEvent(FsEvent ev, std::string_view path, int64_t bytes = 0, bool ok = true)
{
    auto& ts = FsTraceState::Get();
    if (!ts.enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lk(ts.mtx);
    if (ts.entries.size() >= FsTraceState::MAX) ts.entries.pop_front();
    using Clock = std::chrono::steady_clock;
    ts.entries.push_back({
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count()),
        ev, std::string(path), bytes, ok
    });
}

} // namespace (Phase 6 helpers)

const char* FsEventName(FsEvent e)
{
    switch (e) {
        case FsEvent::Open:   return "Open";
        case FsEvent::Close:  return "Close";
        case FsEvent::Read:   return "Read";
        case FsEvent::Write:  return "Write";
        case FsEvent::Stat:   return "Stat";
        case FsEvent::MkDir:  return "MkDir";
        case FsEvent::Remove: return "Remove";
        case FsEvent::Rename: return "Rename";
        case FsEvent::Mount:  return "Mount";
    }
    return "?";
}

void EnableTracing(bool enable)
{
    FsTraceState::Get().enabled.store(enable, std::memory_order_relaxed);
    PS5X_INFO("[VFS] Tracing %s", enable ? "enabled" : "disabled");
}

bool IsTracingEnabled()
{
    return FsTraceState::Get().enabled.load(std::memory_order_relaxed);
}

std::vector<FsTraceEntry> GetTrace(size_t maxEntries)
{
    auto& ts = FsTraceState::Get();
    std::lock_guard lk(ts.mtx);
    size_t n = std::min(maxEntries, ts.entries.size());
    return std::vector<FsTraceEntry>(ts.entries.end() - static_cast<ptrdiff_t>(n),
                                     ts.entries.end());
}

void ClearTrace()
{
    auto& ts = FsTraceState::Get();
    std::lock_guard lk(ts.mtx);
    ts.entries.clear();
}

std::string CreateTempFile(std::string_view name)
{
    static std::atomic<uint64_t> counter{1};
    std::string guestPath = "/temp/";
    if (name.empty()) {
        guestPath += "tmp_" + std::to_string(counter.fetch_add(1));
    } else {
        guestPath += std::string(name);
    }
    // Ensure temp mount exists
    auto& st = VfsState::Get();
    {
        std::lock_guard lk(st.mtx);
        auto& m = st.mounts[static_cast<size_t>(MountPoint::Temp)];
        if (!m.valid) {
            // Mount a temp dir on the host
            auto tmpDir = std::filesystem::temp_directory_path() / "ps5x_vfs_temp";
            std::filesystem::create_directories(tmpDir);
            m.prefix   = "/temp";
            m.hostPath = tmpDir;
            m.valid    = true;
            m.readOnly = false;
        }
    }
    // Create the file
    auto resolved = Resolve(guestPath);
    if (resolved) {
        std::ofstream f(resolved->string());
        (void)f;
    }
    TraceEvent(FsEvent::Open, guestPath, 0, true);
    PS5X_DEBUG("[VFS] CreateTempFile → %s", guestPath.c_str());
    return guestPath;
}

uint64_t GetOpenFileCount()
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    return static_cast<uint64_t>(st.files.size());
}

bool IsReadOnly(MountPoint mp)
{
    auto& st = VfsState::Get();
    std::lock_guard lk(st.mtx);
    auto idx = static_cast<size_t>(mp);
    if (idx >= st.mounts.size()) return true;
    return st.mounts[idx].readOnly;
}


} // namespace PS5x::Filesystem
