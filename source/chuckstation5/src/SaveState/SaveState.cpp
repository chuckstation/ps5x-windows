// ChuckStation5 – Save State Manager implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "ChuckStation5/SaveState/SaveState.h"
#include "ChuckStation5/Cpu/Cpu.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/GPU/GPU.h"
#include "ChuckStation5/Logger/Logger.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

namespace ChuckStation5::SaveState {

// ── CRC32 ─────────────────────────────────────────────────────────────────
static uint32_t Crc32(const uint8_t* data, size_t length)
{
    // Generate CRC32 lookup table (ISO 3309 / ITU-T V.42 polynomial)
    static uint32_t table[256];
    static bool tableReady = false;
    if (!tableReady) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        tableReady = true;
    }
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

// ── Serialisation helpers ─────────────────────────────────────────────────
struct Blob {
    std::vector<uint8_t> data;

    void Write(const void* src, size_t len) {
        const auto* p = static_cast<const uint8_t*>(src);
        data.insert(data.end(), p, p + len);
    }

    template <typename T>
    void WriteT(const T& v) {
        Write(&v, sizeof(T));
    }

    // Write a length-prefixed string
    void WriteStr(const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        WriteT(len);
        if (len > 0) Write(s.data(), len);
    }
};

struct BlobReader {
    const uint8_t* data;
    size_t         size;
    size_t         offset = 0;

    bool Read(void* dst, size_t len) {
        if (offset + len > size) return false;
        std::memcpy(dst, data + offset, len);
        offset += len;
        return true;
    }

    template <typename T>
    bool ReadT(T& v) {
        return Read(&v, sizeof(T));
    }

    bool ReadStr(std::string& s) {
        uint32_t len = 0;
        if (!ReadT(len)) return false;
        if (offset + len > size) return false;
        s.assign(reinterpret_cast<const char*>(data + offset), len);
        offset += len;
        return true;
    }

    bool Eof() const { return offset >= size; }
};

// ── Memory region serialisation format ────────────────────────────────────
// [base:8][size:8][prot:4][type:1][tag_len:4][tag_data:tag_len][page_data:size]

struct SerializedRegion {
    uintptr_t             base;
    size_t                size;
    uint32_t              prot;   // cast from Memory::Prot
    uint8_t               type;   // cast from Memory::AllocType
    std::string           tag;
    std::vector<uint8_t>  pageData;
};

static void SerializeMemoryMap(Blob& blob)
{
    auto regions = Memory::GetRegions();
    uint32_t committedCount = 0;
    for (const auto& r : regions)
        if (r.committed) committedCount++;

    blob.WriteT(committedCount);

    for (const auto& r : regions) {
        if (!r.committed) continue;

        blob.WriteT(r.base);
        blob.WriteT(r.size);
        blob.WriteT(static_cast<uint32_t>(r.prot));
        blob.WriteT(static_cast<uint8_t>(r.type));
        blob.WriteStr(r.tag);

        // Copy page data from the committed region
        // Safe: the region is committed so the host memory is valid
        if (r.size > 0 && r.base != 0) {
            blob.Write(reinterpret_cast<const void*>(r.base), r.size);
        }
    }
}

static bool DeserializeMemoryMap(BlobReader& reader)
{
    uint32_t count = 0;
    if (!reader.ReadT(count)) return false;

    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t base = 0;
        size_t    sz   = 0;
        uint32_t  prot = 0;
        uint8_t   type = 0;
        std::string tag;

        if (!reader.ReadT(base)) return false;
        if (!reader.ReadT(sz))   return false;
        if (!reader.ReadT(prot)) return false;
        if (!reader.ReadT(type)) return false;
        if (!reader.ReadStr(tag)) return false;

        // Read the page data
        std::vector<uint8_t> pageData(sz);
        if (sz > 0 && !reader.Read(pageData.data(), sz)) return false;

        // Re-commit the region and restore its contents
        auto protEnum = static_cast<Memory::Prot>(prot);
        auto typeEnum = static_cast<Memory::AllocType>(type);

        // Check if the region already exists
        auto existing = Memory::FindRegion(base);
        if (!existing.has_value()) {
            // Reserve + commit a new region
            uintptr_t newBase = Memory::Map(base, sz, protEnum, typeEnum, tag);
            if (newBase == 0) {
                CHUCKSTATION5_ERROR("[SaveState] Failed to re-commit region at 0x%016lX size=%zu",
                           static_cast<unsigned long>(base), sz);
                continue;
            }
            base = newBase;
        }

        // Restore page data
        if (sz > 0 && base != 0) {
            std::memcpy(reinterpret_cast<void*>(base), pageData.data(), sz);
        }
    }
    return true;
}

// ── CPU serialisation ─────────────────────────────────────────────────────
// Layout: [gpr:128][rip:8][rflags:8][cs:8][ss:8][xmm:256]
// Total: 128 + 8 + 8 + 8 + 8 + 256 = 416 bytes

static void SerializeCpuContext(Blob& blob)
{
    const auto& ctx = Cpu::GetContextConst();

    // GPRs: 16 x uint64_t
    for (size_t i = 0; i < ctx.gpr.size(); ++i)
        blob.WriteT(ctx.gpr[i]);

    blob.WriteT(ctx.rip);
    blob.WriteT(ctx.rflags);
    blob.WriteT(ctx.cs);
    blob.WriteT(ctx.ss);

    // XMM: 16 x 128-bit (16 bytes each)
    for (size_t i = 0; i < ctx.xmm.size(); ++i)
        blob.Write(ctx.xmm[i].data(), 16);
}

static bool DeserializeCpuContext(BlobReader& reader)
{
    auto& ctx = Cpu::GetContext();

    for (size_t i = 0; i < ctx.gpr.size(); ++i) {
        if (!reader.ReadT(ctx.gpr[i])) return false;
    }

    if (!reader.ReadT(ctx.rip))    return false;
    if (!reader.ReadT(ctx.rflags)) return false;
    if (!reader.ReadT(ctx.cs))     return false;
    if (!reader.ReadT(ctx.ss))     return false;

    for (size_t i = 0; i < ctx.xmm.size(); ++i) {
        if (!reader.Read(ctx.xmm[i].data(), 16)) return false;
    }

    return true;
}

// ── GPU state serialisation ───────────────────────────────────────────────
// Layout: [stats:submits:8][flips:8][barriers:8][fencesSignaled:8][activeQueues:4]
// Minimal: we capture the stats snapshot. Full fence/queue restoration would
// require internal GPU module access.

static void SerializeGpuState(Blob& blob)
{
    auto stats = GPU::GetGpuStats();
    blob.WriteT(stats.submits);
    blob.WriteT(stats.flips);
    blob.WriteT(stats.barriers);
    blob.WriteT(stats.fencesSignaled);
    blob.WriteT(stats.activeQueues);
}

static bool DeserializeGpuState(BlobReader& reader)
{
    // GPU state is largely transient; we read and discard the stats
    // since the GPU queues/fences are recreated on load.
    GPU::GpuStats stats{};
    if (!reader.ReadT(stats.submits))        return false;
    if (!reader.ReadT(stats.flips))          return false;
    if (!reader.ReadT(stats.barriers))       return false;
    if (!reader.ReadT(stats.fencesSignaled)) return false;
    if (!reader.ReadT(stats.activeQueues))   return false;

    CHUCKSTATION5_DEBUG("[SaveState] GPU state restored: submits=%llu flips=%llu queues=%u",
               static_cast<unsigned long long>(stats.submits),
               static_cast<unsigned long long>(stats.flips),
               stats.activeQueues);
    return true;
}

// ── Module state ──────────────────────────────────────────────────────────
namespace {

struct SaveStateManager {
    std::filesystem::path saveDir;
    bool                  initialised = false;
    std::mutex            mtx;

    static SaveStateManager& Get() { static SaveStateManager s; return s; }

    std::filesystem::path SlotPath(int slot) const {
        return saveDir / ("slot_" + std::to_string(slot) + ".p5ss");
    }

    static uint64_t TimestampMs() {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(
                system_clock::now().time_since_epoch()).count());
    }
};

} // anonymous namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(const std::filesystem::path& saveDir)
{
    auto& mgr = SaveStateManager::Get();
    std::lock_guard lk(mgr.mtx);

    mgr.saveDir = saveDir;
    std::error_code ec;
    std::filesystem::create_directories(saveDir, ec);
    if (ec) {
        CHUCKSTATION5_ERROR("[SaveState] Failed to create save directory '%s': %s",
                   saveDir.string().c_str(), ec.message().c_str());
        mgr.initialised = false;
        return false;
    }

    mgr.initialised = true;
    CHUCKSTATION5_INFO("[SaveState] Initialised. Save directory: %s", saveDir.string().c_str());
    return true;
}

void Shutdown()
{
    auto& mgr = SaveStateManager::Get();
    std::lock_guard lk(mgr.mtx);
    mgr.initialised = false;
    CHUCKSTATION5_INFO("[SaveState] Shutdown.");
}

// ── Save ──────────────────────────────────────────────────────────────────
SaveResult Save(int slot, const std::string& description)
{
    auto& mgr = SaveStateManager::Get();
    std::lock_guard lk(mgr.mtx);

    if (!mgr.initialised) {
        CHUCKSTATION5_ERROR("[SaveState] Not initialised.");
        return SaveResult::InvalidState;
    }

    if (slot < 0 || slot > 9) {
        CHUCKSTATION5_ERROR("[SaveState] Invalid slot %d (must be 0-9).", slot);
        return SaveResult::InvalidState;
    }

    // ── Serialize all subsystems ────────────────────────────────────────
    Blob cpuBlob;
    SerializeCpuContext(cpuBlob);

    Blob memBlob;
    SerializeMemoryMap(memBlob);

    Blob gpuBlob;
    SerializeGpuState(gpuBlob);

    // ── Build header ────────────────────────────────────────────────────
    SaveStateHeader header{};
    std::memcpy(header.magic, "ChuckStation5SS\0\0", 8);
    header.version       = 1;
    header.timestamp     = SaveStateManager::TimestampMs();
    header.cpuStateSize  = static_cast<uint32_t>(cpuBlob.data.size());
    header.memoryMapSize = static_cast<uint32_t>(memBlob.data.size());
    header.gpuStateSize  = static_cast<uint32_t>(gpuBlob.data.size());

    // Copy description (truncate if too long)
    {
        size_t len = std::min(description.size(), sizeof(header.description) - 1);
        std::memcpy(header.description, description.data(), len);
        header.description[len] = '\0';
    }

    // Compute CRC32 over payload (cpu + mem + gpu blobs)
    size_t payloadSize = cpuBlob.data.size() + memBlob.data.size() + gpuBlob.data.size();
    std::vector<uint8_t> payload(payloadSize);
    size_t off = 0;
    std::memcpy(payload.data() + off, cpuBlob.data.data(), cpuBlob.data.size()); off += cpuBlob.data.size();
    std::memcpy(payload.data() + off, memBlob.data.data(), memBlob.data.size()); off += memBlob.data.size();
    std::memcpy(payload.data() + off, gpuBlob.data.data(), gpuBlob.data.size()); off += gpuBlob.data.size();
    header.checksum = Crc32(payload.data(), payloadSize);

    // ── Write to file ───────────────────────────────────────────────────
    auto path = mgr.SlotPath(slot);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        CHUCKSTATION5_ERROR("[SaveState] Failed to open '%s' for writing.", path.string().c_str());
        return SaveResult::FileError;
    }

    // Header
    f.write(reinterpret_cast<const char*>(&header), sizeof(header));
    // Payload
    f.write(reinterpret_cast<const char*>(cpuBlob.data.data()), cpuBlob.data.size());
    f.write(reinterpret_cast<const char*>(memBlob.data.data()), memBlob.data.size());
    f.write(reinterpret_cast<const char*>(gpuBlob.data.data()), gpuBlob.data.size());

    if (!f.good()) {
        CHUCKSTATION5_ERROR("[SaveState] Write error on slot %d.", slot);
        return SaveResult::DiskFull;
    }

    f.close();
    CHUCKSTATION5_INFO("[SaveState] Saved slot %d – %zu bytes (cpu=%u mem=%u gpu=%u) crc=0x%08X",
              slot, sizeof(header) + payloadSize,
              header.cpuStateSize, header.memoryMapSize, header.gpuStateSize,
              header.checksum);
    return SaveResult::Ok;
}

// ── Load ──────────────────────────────────────────────────────────────────
LoadResult Load(int slot)
{
    auto& mgr = SaveStateManager::Get();
    std::lock_guard lk(mgr.mtx);

    if (!mgr.initialised) {
        CHUCKSTATION5_ERROR("[SaveState] Not initialised.");
        return LoadResult::CorruptFile;
    }

    auto path = mgr.SlotPath(slot);
    if (!std::filesystem::exists(path)) {
        CHUCKSTATION5_ERROR("[SaveState] Slot %d file not found: %s", slot, path.string().c_str());
        return LoadResult::FileNotFound;
    }

    // ── Read entire file ────────────────────────────────────────────────
    size_t fileSize = std::filesystem::file_size(path);
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        CHUCKSTATION5_ERROR("[SaveState] Failed to open '%s' for reading.", path.string().c_str());
        return LoadResult::CorruptFile;
    }

    std::vector<uint8_t> fileData(fileSize);
    f.read(reinterpret_cast<char*>(fileData.data()), fileSize);
    if (!f.good()) {
        CHUCKSTATION5_ERROR("[SaveState] Read error on slot %d.", slot);
        return LoadResult::CorruptFile;
    }
    f.close();

    // ── Parse header ────────────────────────────────────────────────────
    if (fileSize < sizeof(SaveStateHeader)) {
        CHUCKSTATION5_ERROR("[SaveState] File too small for header (%zu bytes).", fileSize);
        return LoadResult::CorruptFile;
    }

    SaveStateHeader header;
    std::memcpy(&header, fileData.data(), sizeof(header));

    // Validate magic
    if (std::memcmp(header.magic, "ChuckStation5SS\0\0", 8) != 0) {
        CHUCKSTATION5_ERROR("[SaveState] Invalid magic in slot %d.", slot);
        return LoadResult::CorruptFile;
    }

    // Validate version
    if (header.version != 1) {
        CHUCKSTATION5_ERROR("[SaveState] Version mismatch: file=%u expected=1.", header.version);
        return LoadResult::VersionMismatch;
    }

    // ── Validate checksum ───────────────────────────────────────────────
    size_t payloadOffset = sizeof(SaveStateHeader);
    size_t payloadSize = static_cast<size_t>(header.cpuStateSize)
                       + static_cast<size_t>(header.memoryMapSize)
                       + static_cast<size_t>(header.gpuStateSize);

    if (payloadOffset + payloadSize > fileSize) {
        CHUCKSTATION5_ERROR("[SaveState] File truncated: expected %zu bytes, got %zu.",
                   payloadOffset + payloadSize, fileSize);
        return LoadResult::CorruptFile;
    }

    uint32_t computedCrc = Crc32(fileData.data() + payloadOffset, payloadSize);
    if (computedCrc != header.checksum) {
        CHUCKSTATION5_ERROR("[SaveState] CRC mismatch: computed=0x%08X stored=0x%08X.",
                   computedCrc, header.checksum);
        return LoadResult::ChecksumMismatch;
    }

    // ── Restore CPU ─────────────────────────────────────────────────────
    {
        BlobReader reader{fileData.data() + payloadOffset,
                          header.cpuStateSize, 0};
        if (!DeserializeCpuContext(reader)) {
            CHUCKSTATION5_ERROR("[SaveState] Failed to deserialize CPU state.");
            return LoadResult::CorruptFile;
        }
    }

    // ── Restore Memory ──────────────────────────────────────────────────
    {
        size_t memOffset = payloadOffset + header.cpuStateSize;
        BlobReader reader{fileData.data() + memOffset,
                          header.memoryMapSize, 0};
        if (!DeserializeMemoryMap(reader)) {
            CHUCKSTATION5_ERROR("[SaveState] Failed to deserialize memory map.");
            return LoadResult::MemoryConflict;
        }
    }

    // ── Restore GPU ─────────────────────────────────────────────────────
    {
        size_t gpuOffset = payloadOffset + header.cpuStateSize + header.memoryMapSize;
        BlobReader reader{fileData.data() + gpuOffset,
                          header.gpuStateSize, 0};
        if (!DeserializeGpuState(reader)) {
            CHUCKSTATION5_ERROR("[SaveState] Failed to deserialize GPU state.");
            return LoadResult::CorruptFile;
        }
    }

    CHUCKSTATION5_INFO("[SaveState] Loaded slot %d – \"%s\" (ts=%llu)",
              slot, header.description,
              static_cast<unsigned long long>(header.timestamp));
    return LoadResult::Ok;
}

// ── Delete ────────────────────────────────────────────────────────────────
bool Delete(int slot)
{
    auto& mgr = SaveStateManager::Get();
    std::lock_guard lk(mgr.mtx);

    if (!mgr.initialised) return false;

    auto path = mgr.SlotPath(slot);
    if (!std::filesystem::exists(path)) return false;

    std::error_code ec;
    bool ok = std::filesystem::remove(path, ec);
    if (ok) {
        CHUCKSTATION5_INFO("[SaveState] Deleted slot %d.", slot);
    } else {
        CHUCKSTATION5_ERROR("[SaveState] Failed to delete slot %d: %s", slot, ec.message().c_str());
    }
    return ok;
}

// ── Query ─────────────────────────────────────────────────────────────────
bool HasSave(int slot)
{
    auto& mgr = SaveStateManager::Get();
    std::lock_guard lk(mgr.mtx);
    if (!mgr.initialised) return false;
    return std::filesystem::exists(mgr.SlotPath(slot));
}

std::vector<int> ListSaves()
{
    auto& mgr = SaveStateManager::Get();
    std::lock_guard lk(mgr.mtx);

    std::vector<int> result;
    if (!mgr.initialised) return result;

    for (int i = 0; i <= 9; ++i) {
        if (std::filesystem::exists(mgr.SlotPath(i)))
            result.push_back(i);
    }
    return result;
}

SaveStateHeader GetHeader(int slot)
{
    auto& mgr = SaveStateManager::Get();
    std::lock_guard lk(mgr.mtx);

    SaveStateHeader header{};
    if (!mgr.initialised) return header;

    auto path = mgr.SlotPath(slot);
    if (!std::filesystem::exists(path)) return header;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return header;

    f.read(reinterpret_cast<char*>(&header), sizeof(header));
    return header;
}

std::filesystem::path GetSaveDirectory()
{
    auto& mgr = SaveStateManager::Get();
    std::lock_guard lk(mgr.mtx);
    return mgr.saveDir;
}

} // namespace ChuckStation5::SaveState
