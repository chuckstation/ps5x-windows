// PS5x – Loader implementation (Phase 2 – full ELF64 parser)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "PS5x/Loader/Loader.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/Cpu/Cpu.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

namespace PS5x::Loader {

// ── ELF64 constants ───────────────────────────────────────────────────────
namespace Elf64 {
    static constexpr uint32_t MAGIC_LE    = 0x464C457Fu;
    static constexpr uint16_t EM_X86_64  = 62;
    static constexpr uint8_t  ELFCLASS64 = 2;
    static constexpr uint8_t  ELFLSB     = 1;
    static constexpr uint16_t ET_EXEC    = 2;
    static constexpr uint16_t ET_DYN     = 3;
    static constexpr uint16_t ET_SCE_EXEC    = 0xFE00;
    static constexpr uint16_t ET_SCE_DYNEXEC = 0xFE10;
    static constexpr uint16_t ET_SCE_DYNAMIC = 0xFF01;
    static constexpr uint32_t PT_LOAD    = 1;
    static constexpr uint32_t PF_X = 0x1;
    static constexpr uint32_t PF_W = 0x2;
    static constexpr uint32_t PF_R = 0x4;

#pragma pack(push,1)
    struct Ehdr {
        uint8_t  e_ident[16];
        uint16_t e_type, e_machine;
        uint32_t e_version;
        uint64_t e_entry, e_phoff, e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize, e_phentsize, e_phnum;
        uint16_t e_shentsize, e_shnum, e_shstrndx;
    };
    struct Phdr {
        uint32_t p_type, p_flags;
        uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
    };
    struct Shdr {
        uint32_t sh_name;
        uint32_t sh_type;
        uint64_t sh_flags;
        uint64_t sh_addr;
        uint64_t sh_offset;
        uint64_t sh_size;
        uint32_t sh_link;
        uint32_t sh_info;
        uint64_t sh_addralign;
        uint64_t sh_entsize;
    };
    struct Sym {
        uint32_t st_name;
        uint8_t  st_info;
        uint8_t  st_other;
        uint16_t st_shndx;
        uint64_t st_value;
        uint64_t st_size;
    };
#pragma pack(pop)
    static_assert(sizeof(Ehdr)==64,""); static_assert(sizeof(Phdr)==56,"");
    static_assert(sizeof(Shdr)==64,""); static_assert(sizeof(Sym)==24,"");
}

// ── helpers ───────────────────────────────────────────────────────────────
namespace {

template<typename T>
bool ReadAt(std::ifstream& f, uint64_t off, T& v) {
    f.seekg(static_cast<std::streamoff>(off));
    return !!f.read(reinterpret_cast<char*>(&v), sizeof(T));
}
bool ReadBytes(std::ifstream& f, uint64_t off, void* buf, size_t sz) {
    f.seekg(static_cast<std::streamoff>(off));
    return !!f.read(static_cast<char*>(buf), static_cast<std::streamsize>(sz));
}

Memory::Prot FlagsToProt(uint32_t fl) {
    Memory::Prot p = Memory::Prot::None;
    if (fl & Elf64::PF_R) p = p | Memory::Prot::Read;
    if (fl & Elf64::PF_W) p = p | Memory::Prot::Write;
    if (fl & Elf64::PF_X) p = p | Memory::Prot::Exec;
    return p;
}

struct State {
    ExecutableInfo current;
    std::mutex     mtx;
    static State& Get() { static State s; return s; }
};

} // namespace

// ── public ────────────────────────────────────────────────────────────────
const char* LoadResultStr(LoadResult r) {
    switch(r){
        case LoadResult::Ok:                   return "Ok";
        case LoadResult::FileNotFound:         return "FileNotFound";
        case LoadResult::InvalidElf:           return "InvalidElf";
        case LoadResult::UnsupportedArch:      return "UnsupportedArch";
        case LoadResult::MissingSymbol:        return "MissingSymbol";
        case LoadResult::MemoryError:          return "MemoryError";
        case LoadResult::FirmwareRequired:     return "FirmwareRequired";
        case LoadResult::InvalidProgramHeader: return "InvalidProgramHeader";
        case LoadResult::RelocationFailed:     return "RelocationFailed";
        case LoadResult::AlreadyLoaded:        return "AlreadyLoaded";
        case LoadResult::NotLoaded:            return "NotLoaded";
        case LoadResult::IoError:              return "IoError";
    }
    return "Unknown";
}

void Init()     { PS5X_INFO("[Loader] Initialised."); }
void Shutdown() { Reset(); PS5X_INFO("[Loader] Shutdown."); }

void Reset() {
    auto& st = State::Get();
    std::lock_guard lk(st.mtx);
    for (auto& seg : st.current.segments)
        if (seg.hostBase && seg.memsz)
            Memory::Unmap(seg.hostBase, static_cast<size_t>(seg.memsz));
    st.current = ExecutableInfo{};
    PS5X_INFO("[Loader] Reset.");
}

LoadResult InspectElf(const std::filesystem::path& path, ExecutableInfo& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        PS5X_ERROR("[Loader] Not found: %s", path.string().c_str());
        return LoadResult::FileNotFound;
    }

    Elf64::Ehdr eh{};
    if (!ReadAt(f,0,eh)) return LoadResult::IoError;

    if (*reinterpret_cast<const uint32_t*>(eh.e_ident) != Elf64::MAGIC_LE)
        return LoadResult::InvalidElf;
    if (eh.e_ident[4] != Elf64::ELFCLASS64) return LoadResult::InvalidElf;
    if (eh.e_ident[5] != Elf64::ELFLSB)     return LoadResult::InvalidElf;
    if (eh.e_machine   != Elf64::EM_X86_64)  return LoadResult::UnsupportedArch;

    bool isSce = (eh.e_type==Elf64::ET_SCE_EXEC ||
                  eh.e_type==Elf64::ET_SCE_DYNEXEC ||
                  eh.e_type==Elf64::ET_SCE_DYNAMIC);
    bool isStd = (eh.e_type==Elf64::ET_EXEC || eh.e_type==Elf64::ET_DYN);
    if (!isSce && !isStd) return LoadResult::InvalidElf;

    out.path       = path;
    out.entryPoint = eh.e_entry;
    out.isPic      = (eh.e_type==Elf64::ET_DYN ||
                      eh.e_type==Elf64::ET_SCE_DYNEXEC ||
                      eh.e_type==Elf64::ET_SCE_DYNAMIC);

    uint64_t vaMin = UINT64_MAX, vaMax = 0;
    out.segments.clear();

    for (uint16_t i = 0; i < eh.e_phnum; ++i) {
        Elf64::Phdr ph{};
        if (!ReadAt(f, eh.e_phoff + i*sizeof(Elf64::Phdr), ph)) continue;
        if (ph.p_type != Elf64::PT_LOAD) continue;

        Segment seg;
        seg.vaddr=ph.p_vaddr; seg.paddr=ph.p_offset;
        seg.filesz=ph.p_filesz; seg.memsz=ph.p_memsz;
        seg.flags=ph.p_flags; seg.type=ph.p_type;
        seg.align = ph.p_align ? ph.p_align : Memory::PAGE_SIZE;
        out.segments.push_back(seg);

        if (ph.p_vaddr < vaMin) vaMin = ph.p_vaddr;
        uint64_t end = ph.p_vaddr + ph.p_memsz;
        if (end > vaMax) vaMax = end;
    }

    if (vaMin != UINT64_MAX) out.imageBase = vaMin;
    out.imageSize = (vaMax > vaMin) ? vaMax - vaMin : 0;

    // Parse section headers, symbols, and relocations
    std::vector<Elf64::Shdr> shdrs;
    if (eh.e_shnum > 0 && eh.e_shoff > 0) {
        shdrs.resize(eh.e_shnum);
        for (uint16_t i = 0; i < eh.e_shnum; ++i) {
            if (!ReadAt(f, eh.e_shoff + i * sizeof(Elf64::Shdr), shdrs[i])) {
                shdrs.clear();
                break;
            }
        }
    }

    std::vector<char> shstrtab;
    if (!shdrs.empty() && eh.e_shstrndx < shdrs.size()) {
        const auto& sh = shdrs[eh.e_shstrndx];
        shstrtab.resize(static_cast<size_t>(sh.sh_size));
        ReadBytes(f, sh.sh_offset, shstrtab.data(), shstrtab.size());
    }

    out.symbols.clear();
    out.relaSections.clear();

    for (const auto& sh : shdrs) {
        if (sh.sh_type == 2 || sh.sh_type == 11) { // SHT_SYMTAB (2) or SHT_DYNSYM (11)
            std::vector<char> strtab;
            if (sh.sh_link < shdrs.size()) {
                const auto& strsh = shdrs[sh.sh_link];
                strtab.resize(static_cast<size_t>(strsh.sh_size));
                ReadBytes(f, strsh.sh_offset, strtab.data(), strtab.size());
            }

            size_t symCount = sh.sh_size / sizeof(Elf64::Sym);
            for (size_t i = 0; i < symCount; ++i) {
                Elf64::Sym elfsym{};
                if (ReadAt(f, sh.sh_offset + i * sizeof(Elf64::Sym), elfsym)) {
                    Symbol sym;
                    if (elfsym.st_name < strtab.size()) {
                        sym.name = &strtab[elfsym.st_name];
                    }
                    sym.value = elfsym.st_value;
                    sym.size = elfsym.st_size;
                    sym.binding = elfsym.st_info >> 4;
                    sym.type = elfsym.st_info & 0xF;
                    sym.visibility = elfsym.st_other & 0x3;
                    sym.shndx = elfsym.st_shndx;
                    out.symbols.push_back(sym);
                }
            }
        } else if (sh.sh_type == 4) { // SHT_RELA (4)
            RelaSection rs;
            if (sh.sh_name < shstrtab.size()) {
                rs.name = &shstrtab[sh.sh_name];
            } else {
                rs.name = "rela";
            }
            rs.size = sh.sh_size;
            rs.data.resize(static_cast<size_t>(sh.sh_size));
            ReadBytes(f, sh.sh_offset, rs.data.data(), rs.data.size());
            out.relaSections.push_back(std::move(rs));
        }
    }

    PS5X_INFO("[Loader] Inspect OK: %s  entry=0x%llx segs=%zu PIC=%d symbols=%zu relas=%zu",
              path.filename().string().c_str(),
              static_cast<unsigned long long>(out.entryPoint),
              out.segments.size(), out.isPic?1:0,
              out.symbols.size(), out.relaSections.size());
    return LoadResult::Ok;
}

LoadResult ValidateExecutable(const ExecutableInfo& info) {
    if (info.segments.empty()) return LoadResult::InvalidProgramHeader;
    return LoadResult::Ok;
}

LoadResult MapSegments(ExecutableInfo& info, const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return LoadResult::FileNotFound;

    uint64_t loadBias = 0;

    for (auto& seg : info.segments) {
        if (seg.memsz == 0) continue;

        size_t align    = static_cast<size_t>(seg.align ? seg.align : Memory::PAGE_SIZE);
        uint64_t aVa    = seg.vaddr & ~static_cast<uint64_t>(align-1);
        uint64_t aEnd   = (seg.vaddr + seg.memsz + align - 1) & ~static_cast<uint64_t>(align-1);
        size_t   mapSz  = static_cast<size_t>(aEnd - aVa);

        uintptr_t base = Memory::Map(
            static_cast<uintptr_t>(loadBias + aVa), mapSz,
            Memory::Prot::RW,
            (seg.flags & Elf64::PF_X) ? Memory::AllocType::Code : Memory::AllocType::Data,
            path.filename().string());

        if (!base) return LoadResult::MemoryError;
        seg.hostBase = base;

        if (info.isPic && loadBias == 0)
            loadBias = base - aVa;

        if (seg.filesz > 0) {
            std::vector<uint8_t> buf(static_cast<size_t>(seg.filesz));
            if (!ReadBytes(f, seg.paddr, buf.data(), buf.size()))
                return LoadResult::IoError;
            uintptr_t dest = base + static_cast<size_t>(seg.vaddr - aVa);
            std::memcpy(reinterpret_cast<void*>(dest), buf.data(), buf.size());
        }

        if (seg.memsz > seg.filesz) {
            uintptr_t bss = base + static_cast<size_t>(seg.vaddr - aVa + seg.filesz);
            std::memset(reinterpret_cast<void*>(bss), 0,
                        static_cast<size_t>(seg.memsz - seg.filesz));
        }

        Memory::Protect(base, mapSz, FlagsToProt(seg.flags));
        PS5X_INFO("[Loader]  LOAD host=0x%zx va=0x%llx sz=0x%zx flags=%c%c%c",
                  base, static_cast<unsigned long long>(seg.vaddr), mapSz,
                  (seg.flags&Elf64::PF_R)?'R':'-',
                  (seg.flags&Elf64::PF_W)?'W':'-',
                  (seg.flags&Elf64::PF_X)?'X':'-');
    }

    if (info.isPic) info.entryPoint += loadBias;
    info.imageBase = loadBias;

    // Adjust relocation offsets relative to imageBase
    for (auto& rs : info.relaSections) {
        if (!rs.data.empty()) {
            rs.offset = reinterpret_cast<uintptr_t>(rs.data.data()) - info.imageBase;
        }
    }

    info.loaded    = true;
    return LoadResult::Ok;
}

LoadResult LoadExecutable(const std::filesystem::path& path, ExecutableInfo& out) {
    PS5X_INFO("[Loader] LoadExecutable: %s", path.string().c_str());
    if (auto r = InspectElf(path, out);        r != LoadResult::Ok) return r;
    if (auto r = ValidateExecutable(out);      r != LoadResult::Ok) return r;
    if (auto r = MapSegments(out, path);       r != LoadResult::Ok) return r;
    auto& st = State::Get();
    std::lock_guard lk(st.mtx);
    st.current = out;
    PS5X_INFO("[Loader] Loaded. entry=0x%llx base=0x%llx",
              static_cast<unsigned long long>(out.entryPoint),
              static_cast<unsigned long long>(out.imageBase));
    return LoadResult::Ok;
}

LoadResult LoadFromMemory(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(Elf64::Ehdr)) {
        return LoadResult::InvalidElf;
    }

    const auto* eh = reinterpret_cast<const Elf64::Ehdr*>(data);
    if (*reinterpret_cast<const uint32_t*>(eh->e_ident) != Elf64::MAGIC_LE)
        return LoadResult::InvalidElf;
    if (eh->e_ident[4] != Elf64::ELFCLASS64) return LoadResult::InvalidElf;
    if (eh->e_ident[5] != Elf64::ELFLSB)     return LoadResult::InvalidElf;
    if (eh->e_machine   != Elf64::EM_X86_64)  return LoadResult::UnsupportedArch;

    bool isSce = (eh->e_type==Elf64::ET_SCE_EXEC ||
                  eh->e_type==Elf64::ET_SCE_DYNEXEC ||
                  eh->e_type==Elf64::ET_SCE_DYNAMIC);
    bool isStd = (eh->e_type==Elf64::ET_EXEC || eh->e_type==Elf64::ET_DYN);
    if (!isSce && !isStd) return LoadResult::InvalidElf;

    ExecutableInfo info;
    info.entryPoint = eh->e_entry;
    info.isPic      = (eh->e_type==Elf64::ET_DYN ||
                       eh->e_type==Elf64::ET_SCE_DYNEXEC ||
                       eh->e_type==Elf64::ET_SCE_DYNAMIC);

    if (eh->e_phoff == 0 || eh->e_phnum == 0 ||
        eh->e_phoff + static_cast<uint64_t>(eh->e_phnum) * sizeof(Elf64::Phdr) > size) {
        return LoadResult::InvalidProgramHeader;
    }

    uint64_t vaMin = UINT64_MAX, vaMax = 0;
    const auto* phdrs = reinterpret_cast<const Elf64::Phdr*>(data + eh->e_phoff);

    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const auto& ph = phdrs[i];
        if (ph.p_type != Elf64::PT_LOAD) continue;

        if (ph.p_offset + ph.p_filesz > size) {
            return LoadResult::InvalidProgramHeader;
        }

        Segment seg;
        seg.vaddr = ph.p_vaddr; seg.paddr = ph.p_offset;
        seg.filesz = ph.p_filesz; seg.memsz = ph.p_memsz;
        seg.flags = ph.p_flags; seg.type = ph.p_type;
        seg.align = ph.p_align ? ph.p_align : Memory::PAGE_SIZE;
        info.segments.push_back(seg);

        if (ph.p_vaddr < vaMin) vaMin = ph.p_vaddr;
        uint64_t end = ph.p_vaddr + ph.p_memsz;
        if (end > vaMax) vaMax = end;
    }

    if (info.segments.empty()) return LoadResult::InvalidProgramHeader;

    if (vaMin != UINT64_MAX) info.imageBase = vaMin;
    info.imageSize = (vaMax > vaMin) ? vaMax - vaMin : 0;

    uint64_t loadBias = 0;
    for (auto& seg : info.segments) {
        if (seg.memsz == 0) continue;

        size_t align  = static_cast<size_t>(seg.align ? seg.align : Memory::PAGE_SIZE);
        uint64_t aVa  = seg.vaddr & ~static_cast<uint64_t>(align - 1);
        uint64_t aEnd = (seg.vaddr + seg.memsz + align - 1) & ~static_cast<uint64_t>(align - 1);
        size_t mapSz  = static_cast<size_t>(aEnd - aVa);

        uintptr_t base = Memory::Map(
            static_cast<uintptr_t>(loadBias + aVa), mapSz,
            Memory::Prot::RW,
            (seg.flags & Elf64::PF_X) ? Memory::AllocType::Code : Memory::AllocType::Data,
            "MemoryElf");

        if (!base) return LoadResult::MemoryError;
        seg.hostBase = base;

        if (info.isPic && loadBias == 0)
            loadBias = base - aVa;

        if (seg.filesz > 0) {
            uintptr_t dest = base + static_cast<size_t>(seg.vaddr - aVa);
            std::memcpy(reinterpret_cast<void*>(dest), data + seg.paddr, static_cast<size_t>(seg.filesz));
        }

        if (seg.memsz > seg.filesz) {
            uintptr_t bss = base + static_cast<size_t>(seg.vaddr - aVa + seg.filesz);
            std::memset(reinterpret_cast<void*>(bss), 0, static_cast<size_t>(seg.memsz - seg.filesz));
        }

        Memory::Protect(base, mapSz, FlagsToProt(seg.flags));
    }

    if (info.isPic) info.entryPoint += loadBias;
    info.imageBase = loadBias;
    info.loaded = true;

    auto& st = State::Get();
    std::lock_guard lk(st.mtx);
    st.current = std::move(info);

    return LoadResult::Ok;
}

LoadResult LoadFromPath(const std::string& path) {
    if (path.empty()) return LoadResult::FileNotFound;
    if (!std::filesystem::exists(path)) return LoadResult::FileNotFound;
    ExecutableInfo info;
    return LoadExecutable(path, info);
}

LoadResult UnloadExecutable(ExecutableInfo& info) {
    if (!info.loaded) return LoadResult::NotLoaded;
    for (auto& seg : info.segments)
        if (seg.hostBase && seg.memsz)
            Memory::Unmap(seg.hostBase, static_cast<size_t>(seg.memsz));
    info.loaded = false;
    return LoadResult::Ok;
}

uint64_t GetEntryPoint() { return State::Get().current.entryPoint; }

LoadResult Execute() {
    auto& st = State::Get();
    std::lock_guard lk(st.mtx);
    if (!st.current.loaded) return LoadResult::NotLoaded;
    uint64_t entry = st.current.entryPoint;
    PS5X_INFO("[Loader] Launching guest entry 0x%llx on Win32 thread.",
              static_cast<unsigned long long>(entry));
#if defined(_WIN32)
    // Spin up a Win32 thread that jumps to the guest entry point.
    // The CPU interpreter runs in the caller thread (Cpu::Run()) so
    // we simply queue the RIP and let the emulation loop handle it.
    // For direct native execution (non-interpreted), we would use:
    //   HANDLE h = CreateThread(nullptr, 0,
    //       reinterpret_cast<LPTHREAD_START_ROUTINE>(entry), nullptr, 0, nullptr);
    // However, under the PS5x interpreter model, execution is driven by
    // Cpu::SetRip(entry) + Cpu::Run() from the main loop.
    Cpu::SetRip(entry);
    PS5X_INFO("[Loader] RIP set to guest entry 0x%llx. Call Cpu::Run() to execute.",
              static_cast<unsigned long long>(entry));
#else
    Cpu::SetRip(entry);
#endif
    return LoadResult::Ok;
}

LoadResult ValidateFirmware(const std::filesystem::path& p) {
    if (p.empty() || !std::filesystem::exists(p)) {
        PS5X_ERROR("[Loader] Firmware absent: '%s'. PS5x does NOT supply firmware.",
                   p.string().c_str());
        return LoadResult::FirmwareRequired;
    }
    return LoadResult::Ok;
}

LoadResult LoadParamSfo(const std::filesystem::path& sfoPath, ExecutableInfo& info) {
    std::ifstream f(sfoPath, std::ios::binary);
    if (!f.is_open()) return LoadResult::FileNotFound;

    uint32_t magic=0;
    if (!ReadAt(f,0,magic) || magic!=0x46535000u) return LoadResult::InvalidElf;

    uint32_t keyOff=0, dataOff=0, nEnt=0;
    ReadAt(f,8,keyOff); ReadAt(f,12,dataOff); ReadAt(f,16,nEnt);

#pragma pack(push,1)
    struct E { uint16_t keyOff,fmt; uint32_t dlen,dmax,doff; };
#pragma pack(pop)

    for (uint32_t i=0; i<nEnt; ++i) {
        E e{}; if (!ReadAt(f, 20+i*sizeof(E), e)) break;
        f.seekg(keyOff+e.keyOff);
        std::string key; std::getline(f,key,'\0');
        if (e.fmt==0x0204) {
            std::vector<char> buf(e.dlen+1,0);
            ReadBytes(f,dataOff+e.doff,buf.data(),e.dlen);
            std::string val=buf.data();
            if      (key=="TITLE_ID")   info.titleId    = val;
            else if (key=="APP_VER")    info.appVersion = val;
            else if (key=="CONTENT_ID") info.contentId  = val;
        }
    }
    PS5X_INFO("[Loader] SFO title=%s ver=%s",
              info.titleId.c_str(), info.appVersion.c_str());
    return LoadResult::Ok;
}

} // namespace PS5x::Loader
