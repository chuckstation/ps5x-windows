// PS5x – Loader unit tests (Phase 2)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/Loader/Loader.h"

#include <filesystem>
#include <fstream>
#include <vector>

// ── helpers ───────────────────────────────────────────────────────────────

// Minimal valid ELF64 x86-64 executable (one PT_LOAD, no code)
static std::filesystem::path WriteTinyElf()
{
    auto path = std::filesystem::temp_directory_path() / "ps5x_test.elf";

#pragma pack(push,1)
    struct Ehdr {
        uint8_t  e_ident[16]; uint16_t e_type,e_machine; uint32_t e_version;
        uint64_t e_entry,e_phoff,e_shoff; uint32_t e_flags;
        uint16_t e_ehsize,e_phentsize,e_phnum,e_shentsize,e_shnum,e_shstrndx;
    };
    struct Phdr {
        uint32_t p_type,p_flags;
        uint64_t p_offset,p_vaddr,p_paddr,p_filesz,p_memsz,p_align;
    };
#pragma pack(pop)

    Ehdr eh{}; Phdr ph{};

    // Magic + class/data/version
    eh.e_ident[0]=0x7F; eh.e_ident[1]='E'; eh.e_ident[2]='L'; eh.e_ident[3]='F';
    eh.e_ident[4]=2;    // ELFCLASS64
    eh.e_ident[5]=1;    // ELFDATA2LSB
    eh.e_ident[6]=1;    // EV_CURRENT

    eh.e_type        = 2;   // ET_EXEC
    eh.e_machine     = 62;  // EM_X86_64
    eh.e_version     = 1;
    eh.e_entry       = 0x401000;
    eh.e_phoff       = sizeof(Ehdr);
    eh.e_ehsize      = sizeof(Ehdr);
    eh.e_phentsize   = sizeof(Phdr);
    eh.e_phnum       = 1;

    ph.p_type   = 1;           // PT_LOAD
    ph.p_flags  = 0x5;         // PF_R | PF_X
    ph.p_offset = 0;
    ph.p_vaddr  = 0x400000;
    ph.p_paddr  = 0x400000;
    ph.p_filesz = sizeof(Ehdr) + sizeof(Phdr);
    ph.p_memsz  = sizeof(Ehdr) + sizeof(Phdr);
    ph.p_align  = 0x1000;

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&eh), sizeof(eh));
    f.write(reinterpret_cast<const char*>(&ph), sizeof(ph));
    return path;
}

// ── tests ─────────────────────────────────────────────────────────────────

TEST_CASE("Loader – LoadResultStr coverage", "[loader]")
{
    REQUIRE(std::string(PS5x::Loader::LoadResultStr(PS5x::Loader::LoadResult::Ok)) == "Ok");
    REQUIRE(std::string(PS5x::Loader::LoadResultStr(PS5x::Loader::LoadResult::FileNotFound)) == "FileNotFound");
    REQUIRE(std::string(PS5x::Loader::LoadResultStr(PS5x::Loader::LoadResult::FirmwareRequired)) == "FirmwareRequired");
}

TEST_CASE("Loader – InspectElf nonexistent returns FileNotFound", "[loader]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Memory::Init();
    PS5x::Loader::Init();

    PS5x::Loader::ExecutableInfo info;
    auto r = PS5x::Loader::InspectElf("/does/not/exist.elf", info);
    REQUIRE(r == PS5x::Loader::LoadResult::FileNotFound);

    PS5x::Loader::Shutdown();
    PS5x::Memory::Reset();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Loader – InspectElf parses tiny ELF", "[loader]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Memory::Init();
    PS5x::Loader::Init();

    auto path = WriteTinyElf();
    PS5x::Loader::ExecutableInfo info;
    auto r = PS5x::Loader::InspectElf(path, info);
    REQUIRE(r == PS5x::Loader::LoadResult::Ok);
    REQUIRE(info.entryPoint == 0x401000);
    REQUIRE(info.segments.size() == 1);
    REQUIRE(!info.isPic);

    std::filesystem::remove(path);
    PS5x::Loader::Shutdown();
    PS5x::Memory::Reset();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Loader – ValidateExecutable rejects empty info", "[loader]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Memory::Init();

    PS5x::Loader::ExecutableInfo info;
    auto r = PS5x::Loader::ValidateExecutable(info);
    REQUIRE(r == PS5x::Loader::LoadResult::InvalidProgramHeader);

    PS5x::Memory::Reset();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Loader – LoadExecutable maps tiny ELF", "[loader]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Memory::Init();
    PS5x::Loader::Init();

    auto path = WriteTinyElf();
    PS5x::Loader::ExecutableInfo info;
    auto r = PS5x::Loader::LoadExecutable(path, info);
    REQUIRE(r == PS5x::Loader::LoadResult::Ok);
    REQUIRE(info.loaded);
    REQUIRE(info.segments[0].hostBase != 0);
    REQUIRE(PS5x::Loader::GetEntryPoint() != 0);

    PS5x::Loader::UnloadExecutable(info);
    REQUIRE(!info.loaded);

    std::filesystem::remove(path);
    PS5x::Loader::Shutdown();
    PS5x::Memory::Shutdown();
    PS5x::Logger::Shutdown();
}

TEST_CASE("Loader – firmware validation rejects empty path", "[loader]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    REQUIRE(PS5x::Loader::ValidateFirmware("") == PS5x::Loader::LoadResult::FirmwareRequired);
    REQUIRE(PS5x::Loader::ValidateFirmware("/nonexistent/fw") == PS5x::Loader::LoadResult::FirmwareRequired);
    PS5x::Logger::Shutdown();
}
