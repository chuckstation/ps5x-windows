// ChuckStation5 – Loader unit tests (Phase 2)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/Loader/Loader.h"

#include <filesystem>
#include <fstream>
#include <vector>

// ── helpers ───────────────────────────────────────────────────────────────

// Minimal valid ELF64 x86-64 executable (one PT_LOAD, no code)
static std::filesystem::path WriteTinyElf()
{
    auto path = std::filesystem::temp_directory_path() / "chuckstation5_test.elf";

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
    REQUIRE(std::string(ChuckStation5::Loader::LoadResultStr(ChuckStation5::Loader::LoadResult::Ok)) == "Ok");
    REQUIRE(std::string(ChuckStation5::Loader::LoadResultStr(ChuckStation5::Loader::LoadResult::FileNotFound)) == "FileNotFound");
    REQUIRE(std::string(ChuckStation5::Loader::LoadResultStr(ChuckStation5::Loader::LoadResult::FirmwareRequired)) == "FirmwareRequired");
}

TEST_CASE("Loader – InspectElf nonexistent returns FileNotFound", "[loader]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();
    ChuckStation5::Loader::Init();

    ChuckStation5::Loader::ExecutableInfo info;
    auto r = ChuckStation5::Loader::InspectElf("/does/not/exist.elf", info);
    REQUIRE(r == ChuckStation5::Loader::LoadResult::FileNotFound);

    ChuckStation5::Loader::Shutdown();
    ChuckStation5::Memory::Reset();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Loader – InspectElf parses tiny ELF", "[loader]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();
    ChuckStation5::Loader::Init();

    auto path = WriteTinyElf();
    ChuckStation5::Loader::ExecutableInfo info;
    auto r = ChuckStation5::Loader::InspectElf(path, info);
    REQUIRE(r == ChuckStation5::Loader::LoadResult::Ok);
    REQUIRE(info.entryPoint == 0x401000);
    REQUIRE(info.segments.size() == 1);
    REQUIRE(!info.isPic);

    std::filesystem::remove(path);
    ChuckStation5::Loader::Shutdown();
    ChuckStation5::Memory::Reset();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Loader – ValidateExecutable rejects empty info", "[loader]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();

    ChuckStation5::Loader::ExecutableInfo info;
    auto r = ChuckStation5::Loader::ValidateExecutable(info);
    REQUIRE(r == ChuckStation5::Loader::LoadResult::InvalidProgramHeader);

    ChuckStation5::Memory::Reset();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Loader – LoadExecutable maps tiny ELF", "[loader]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();
    ChuckStation5::Loader::Init();

    auto path = WriteTinyElf();
    ChuckStation5::Loader::ExecutableInfo info;
    auto r = ChuckStation5::Loader::LoadExecutable(path, info);
    REQUIRE(r == ChuckStation5::Loader::LoadResult::Ok);
    REQUIRE(info.loaded);
    REQUIRE(info.segments[0].hostBase != 0);
    REQUIRE(ChuckStation5::Loader::GetEntryPoint() != 0);

    ChuckStation5::Loader::UnloadExecutable(info);
    REQUIRE(!info.loaded);

    std::filesystem::remove(path);
    ChuckStation5::Loader::Shutdown();
    ChuckStation5::Memory::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("Loader – firmware validation rejects empty path", "[loader]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    REQUIRE(ChuckStation5::Loader::ValidateFirmware("") == ChuckStation5::Loader::LoadResult::FirmwareRequired);
    REQUIRE(ChuckStation5::Loader::ValidateFirmware("/nonexistent/fw") == ChuckStation5::Loader::LoadResult::FirmwareRequired);
    ChuckStation5::Logger::Shutdown();
}
