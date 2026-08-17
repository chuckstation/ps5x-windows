// PS5x – Execution Framework tests (Phase 4)
// SPDX-License-Identifier: MIT
#include "PS5x/Debugger/Debugger.h"
#include "PS5x/Execution/Execution.h"
#include "PS5x/Filesystem/Filesystem.h"
#include "PS5x/KernelRuntime/KernelRuntime.h"
#include "PS5x/Loader/Loader.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Memory/Memory.h"
#include "PS5x/Process/Process.h"
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace PS5x::Execution;

static fs::path WriteTinyElf() {
  auto path = fs::temp_directory_path() / "ps5x_exec_test.elf";
#pragma pack(push, 1)
  struct Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
  };
  struct Phdr {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
  };
#pragma pack(pop)
  Ehdr eh{};
  Phdr ph{};
  eh.e_ident[0] = 0x7F;
  eh.e_ident[1] = 'E';
  eh.e_ident[2] = 'L';
  eh.e_ident[3] = 'F';
  eh.e_ident[4] = 2;
  eh.e_ident[5] = 1;
  eh.e_ident[6] = 1;
  eh.e_type = 2;
  eh.e_machine = 62;
  eh.e_version = 1;
  eh.e_entry = 0x401000;
  eh.e_phoff = sizeof(Ehdr);
  eh.e_ehsize = sizeof(Ehdr);
  eh.e_phentsize = sizeof(Phdr);
  eh.e_phnum = 1;
  ph.p_type = 1;
  ph.p_flags = 0x5;
  ph.p_vaddr = 0x400000;
  ph.p_paddr = 0x400000;
  ph.p_filesz = sizeof(Ehdr) + sizeof(Phdr);
  ph.p_memsz = sizeof(Ehdr) + sizeof(Phdr);
  ph.p_align = 0x1000;
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char *>(&eh), sizeof(eh));
  f.write(reinterpret_cast<const char *>(&ph), sizeof(ph));
  return path;
}

static void Setup() {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  PS5x::Memory::Init();
  PS5x::KernelRuntime::Init();
  PS5x::Filesystem::Init();
  PS5x::Loader::Init();
  PS5x::Process::Init();
  PS5x::Debugger::Init();
  Init();
}
static void Teardown() {
  Shutdown();
  PS5x::Debugger::Shutdown();
  PS5x::Process::Shutdown();
  PS5x::Loader::Shutdown();
  PS5x::Filesystem::Shutdown();
  PS5x::KernelRuntime::Shutdown();
  PS5x::Memory::Shutdown();
  PS5x::Logger::Shutdown();
}

TEST_CASE("Execution – ExecStateName coverage", "[exec]") {
  REQUIRE(std::string(ExecStateName(ExecState::Idle)) == "Idle");
  REQUIRE(std::string(ExecStateName(ExecState::Running)) == "Running");
  REQUIRE(std::string(ExecStateName(ExecState::Terminated)) == "Terminated");
  REQUIRE(std::string(ExecStateName(ExecState::Faulted)) == "Faulted");
}

TEST_CASE("Execution – Init state is Idle", "[exec]") {
  Setup();
  REQUIRE(GetState() == ExecState::Idle);
  REQUIRE(!IsRunning());
  Teardown();
}

TEST_CASE("Execution – Start without LoadProgram fails gracefully", "[exec]") {
  Setup();
  REQUIRE(!Start());
  REQUIRE(GetState() == ExecState::Idle);
  REQUIRE(!GetLastError().empty());
  Teardown();
}

TEST_CASE("Execution – LoadProgram transitions to Ready", "[exec]") {
  Setup();
  auto elfPath = WriteTinyElf();
  auto content = fs::temp_directory_path() / "ps5x_exec_content";
  fs::create_directories(content);

  LoadOptions opts;
  opts.contentPath = content;

  REQUIRE(LoadProgram(elfPath, opts));
  REQUIRE(GetState() == ExecState::Ready);

  ForceTerminate();
  fs::remove(elfPath);
  fs::remove_all(content);
  Teardown();
}

TEST_CASE("Execution – LoadProgram nonexistent ELF → Faulted", "[exec]") {
  Setup();
  REQUIRE(!LoadProgram("/no/such/file.elf"));
  REQUIRE(GetState() == ExecState::Faulted);
  REQUIRE(!GetLastError().empty());
  Teardown();
}

TEST_CASE("Execution – Start after LoadProgram → Running", "[exec]") {
  Setup();
  auto elfPath = WriteTinyElf();
  auto content = fs::temp_directory_path() / "ps5x_exec_start";
  fs::create_directories(content);

  REQUIRE(LoadProgram(elfPath, {.contentPath = content}));
  REQUIRE(Start());
  auto state = GetState();
  REQUIRE((state == ExecState::Running || state == ExecState::Terminated));

  ForceTerminate();
  fs::remove(elfPath);
  fs::remove_all(content);
  Teardown();
}

TEST_CASE("Execution – StateChange callback fires", "[exec]") {
  Setup();
  std::atomic<int> changes{0};
  OnStateChange([&](ExecState, ExecState) { changes.fetch_add(1); });

  auto elfPath = WriteTinyElf();
  auto content = fs::temp_directory_path() / "ps5x_exec_cb";
  fs::create_directories(content);
  LoadProgram(elfPath, {.contentPath = content});

  REQUIRE(changes.load() >= 1); // Idle→Loading, Loading→Ready

  ForceTerminate();
  fs::remove(elfPath);
  fs::remove_all(content);
  Teardown();
}

TEST_CASE("Execution – GetStats returns valid data after load", "[exec]") {
  Setup();
  auto elfPath = WriteTinyElf();
  auto content = fs::temp_directory_path() / "ps5x_exec_stats";
  fs::create_directories(content);

  LoadProgram(elfPath, {.contentPath = content});
  auto s = GetStats();
  REQUIRE(s.pid != 0);
  REQUIRE(s.loadTimeMs >= 0.0);
  REQUIRE(s.moduleCount >= 1);

  ForceTerminate();
  fs::remove(elfPath);
  fs::remove_all(content);
  Teardown();
}

TEST_CASE("Execution – NotifyFrameRendered increments counter", "[exec]") {
  Setup();
  auto elfPath = WriteTinyElf();
  auto content = fs::temp_directory_path() / "ps5x_exec_frames";
  fs::create_directories(content);

  LoadProgram(elfPath, {.contentPath = content});
  NotifyFrameRendered();
  NotifyFrameRendered();
  NotifyFrameRendered();
  auto s = GetStats();
  REQUIRE(s.framesRendered == 3);

  ForceTerminate();
  fs::remove(elfPath);
  fs::remove_all(content);
  Teardown();
}

TEST_CASE("Execution – NotifySyscall increments counter", "[exec]") {
  Setup();
  auto elfPath = WriteTinyElf();
  auto content = fs::temp_directory_path() / "ps5x_exec_syscall";
  fs::create_directories(content);

  LoadProgram(elfPath, {.contentPath = content});
  for (int i = 0; i < 5; ++i)
    NotifySyscall(i);
  auto s = GetStats();
  REQUIRE(s.syscallsEmulated == 5);

  ForceTerminate();
  fs::remove(elfPath);
  fs::remove_all(content);
  Teardown();
}

TEST_CASE("Execution – Pause and Resume toggle state", "[exec]") {
  Setup();
  auto elfPath = WriteTinyElf();
  auto content = fs::temp_directory_path() / "ps5x_exec_pause";
  fs::create_directories(content);

  LoadProgram(elfPath, {.contentPath = content});
  Start();

  // Only test Pause if we're still Running
  if (GetState() == ExecState::Running) {
    REQUIRE(Pause());
    REQUIRE(GetState() == ExecState::Paused);
    REQUIRE(Resume());
    REQUIRE(GetState() == ExecState::Running);
  }

  ForceTerminate();
  fs::remove(elfPath);
  fs::remove_all(content);
  Teardown();
}
