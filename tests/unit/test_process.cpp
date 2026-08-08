// PS5x – Process Manager unit tests (Phase 3)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/KernelRuntime/KernelRuntime.h"
#include "PS5x/Filesystem/Filesystem.h"
#include "PS5x/Loader/Loader.h"
#include "PS5x/Process/Process.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────

// Write a minimal valid ELF64 x86-64 ET_EXEC to a temp file
static fs::path WriteTinyElf()
{
    auto path = fs::temp_directory_path() / "ps5x_proc_test.elf";
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
    eh.e_ident[0]=0x7F; eh.e_ident[1]='E'; eh.e_ident[2]='L'; eh.e_ident[3]='F';
    eh.e_ident[4]=2; eh.e_ident[5]=1; eh.e_ident[6]=1;
    eh.e_type=2; eh.e_machine=62; eh.e_version=1;
    eh.e_entry=0x401000; eh.e_phoff=sizeof(Ehdr);
    eh.e_ehsize=sizeof(Ehdr); eh.e_phentsize=sizeof(Phdr); eh.e_phnum=1;
    ph.p_type=1; ph.p_flags=0x5; ph.p_vaddr=0x400000; ph.p_paddr=0x400000;
    ph.p_filesz=sizeof(Ehdr)+sizeof(Phdr); ph.p_memsz=sizeof(Ehdr)+sizeof(Phdr);
    ph.p_align=0x1000;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&eh), sizeof(eh));
    f.write(reinterpret_cast<const char*>(&ph), sizeof(ph));
    return path;
}

static void Setup()
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Memory::Init();
    PS5x::KernelRuntime::Init();
    PS5x::Filesystem::Init();
    PS5x::Loader::Init();
    PS5x::Process::Init();
}
static void Teardown()
{
    PS5x::Process::Shutdown();
    PS5x::Loader::Shutdown();
    PS5x::Filesystem::Shutdown();
    PS5x::KernelRuntime::Shutdown();
    PS5x::Memory::Shutdown();
    PS5x::Logger::Shutdown();
}

// ── Tests ─────────────────────────────────────────────────────────────────

TEST_CASE("Process – ProcessStateName coverage", "[proc]")
{
    using namespace PS5x::Process;
    REQUIRE(std::string(ProcessStateName(ProcessState::None))       == "None");
    REQUIRE(std::string(ProcessStateName(ProcessState::Running))    == "Running");
    REQUIRE(std::string(ProcessStateName(ProcessState::Terminated)) == "Terminated");
    REQUIRE(std::string(ProcessStateName(ProcessState::Faulted))    == "Faulted");
}

TEST_CASE("Process – GetCurrentPid returns 0 initially", "[proc]")
{
    Setup();
    REQUIRE(PS5x::Process::GetCurrentPid() == 0);
    Teardown();
}

TEST_CASE("Process – Create returns valid PID for tiny ELF", "[proc]")
{
    Setup();
    auto elfPath = WriteTinyElf();
    auto contentDir = fs::temp_directory_path() / "ps5x_proc_content";
    fs::create_directories(contentDir);

    uint32_t pid = PS5x::Process::Create(elfPath, contentDir, "");
    REQUIRE(pid != 0);
    REQUIRE(PS5x::Process::GetState(pid) == PS5x::Process::ProcessState::Created);
    REQUIRE(PS5x::Process::GetCurrentPid() == pid);

    auto info = PS5x::Process::GetInfo(pid);
    REQUIRE(info.pid == pid);
    REQUIRE(!info.modules.empty());
    REQUIRE(info.modules[0].isMain);

    PS5x::Process::Terminate(pid);
    fs::remove(elfPath);
    fs::remove_all(contentDir);
    Teardown();
}

TEST_CASE("Process – Create fails for nonexistent ELF", "[proc]")
{
    Setup();
    uint32_t pid = PS5x::Process::Create("/no/such/file.elf", "", "");
    REQUIRE(pid == 0);
    Teardown();
}

TEST_CASE("Process – GetState returns None for unknown PID", "[proc]")
{
    Setup();
    REQUIRE(PS5x::Process::GetState(9999) == PS5x::Process::ProcessState::None);
    REQUIRE(!PS5x::Process::IsRunning(9999));
    Teardown();
}

TEST_CASE("Process – Start transitions state to Running", "[proc]")
{
    // Note: Wait() is intentionally not tested here because it depends on
    // KernelRuntime::JoinThread which requires the OS thread to complete.
    // The thread completion path is validated in test_kernel_runtime.cpp.
    Setup();
    auto elfPath = WriteTinyElf();
    auto contentDir = fs::temp_directory_path() / "ps5x_proc_start";
    fs::create_directories(contentDir);

    uint32_t pid = PS5x::Process::Create(elfPath, contentDir, "");
    REQUIRE(pid != 0);
    REQUIRE(PS5x::Process::GetState(pid) == PS5x::Process::ProcessState::Created);

    REQUIRE(PS5x::Process::Start(pid));
    // After Start(), state transitions to Running
    auto state = PS5x::Process::GetState(pid);
    REQUIRE((state == PS5x::Process::ProcessState::Running ||
             state == PS5x::Process::ProcessState::Terminated));

    PS5x::Process::Terminate(pid);
    fs::remove(elfPath);
    fs::remove_all(contentDir);
    Teardown();
}

TEST_CASE("Process – GetModules returns loaded module list", "[proc]")
{
    Setup();
    auto elfPath = WriteTinyElf();
    auto contentDir = fs::temp_directory_path() / "ps5x_proc_mods";
    fs::create_directories(contentDir);

    uint32_t pid = PS5x::Process::Create(elfPath, contentDir, "");
    REQUIRE(pid != 0);

    auto mods = PS5x::Process::GetModules(pid);
    REQUIRE(mods.size() == 1);
    REQUIRE(mods[0].isMain);
    REQUIRE(mods[0].name == elfPath.filename().string());

    PS5x::Process::Terminate(pid);
    fs::remove(elfPath);
    fs::remove_all(contentDir);
    Teardown();
}

TEST_CASE("Process – ExitCallback fires on termination", "[proc]")
{
    Setup();
    bool callbackFired = false;
    uint32_t cbPid = 0; int cbCode = -1;

    PS5x::Process::RegisterExitCallback([&](uint32_t pid, int code){
        callbackFired = true;
        cbPid = pid; cbCode = code;
    });

    auto elfPath = WriteTinyElf();
    auto contentDir = fs::temp_directory_path() / "ps5x_proc_cb";
    fs::create_directories(contentDir);

    uint32_t pid = PS5x::Process::Create(elfPath, contentDir, "");
    REQUIRE(pid != 0);
    PS5x::Process::Terminate(pid);

    REQUIRE(callbackFired);
    REQUIRE(cbPid == pid);

    fs::remove(elfPath);
    fs::remove_all(contentDir);
    Teardown();
}

TEST_CASE("Process – RequestExit transitions state", "[proc]")
{
    Setup();
    auto elfPath = WriteTinyElf();
    uint32_t pid = PS5x::Process::Create(elfPath, "", "");
    REQUIRE(pid != 0);
    REQUIRE(PS5x::Process::RequestExit(pid, 1));
    REQUIRE(PS5x::Process::GetState(pid) == PS5x::Process::ProcessState::Exiting);
    PS5x::Process::Terminate(pid);
    fs::remove(elfPath);
    Teardown();
}
