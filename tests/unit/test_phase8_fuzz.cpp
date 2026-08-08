// PS5x – Phase 8 Fuzz / Robustness tests
// SPDX-License-Identifier: MIT
//
// Validates robustness of loaders and parsers against malformed input.
// These are deterministic property tests (pseudo-random with fixed seeds),
// not libFuzzer harnesses, so they run under the normal test runner.
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Cpu/Cpu.h"
#include "PS5x/Loader/Loader.h"
#include "PS5x/Filesystem/Filesystem.h"
#include "PS5x/CommandProcessor/CommandProcessor.h"
#include "PS5x/GPU/GPU.h"
#include "PS5x/Memory/Memory.h"
#include <cstring>
#include <random>
#include <vector>

using namespace PS5x;

// ── Helper: deterministic random bytes ────────────────────────────────────

static std::vector<uint8_t> RandomBytes(size_t n, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::vector<uint8_t> out(n);
    for (auto& b : out) b = static_cast<uint8_t>(rng() & 0xFF);
    return out;
}

// ── Loader fuzz ───────────────────────────────────────────────────────────

TEST_CASE("Phase8::Fuzz::Loader::RandomBytesDoNotCrash", "[fuzz][phase8]")
{
    Loader::Init();
    Memory::Init();

    for (uint32_t seed = 0; seed < 20; ++seed) {
        auto junk = RandomBytes(512, seed);
        // Attempt to load — must return an error, never crash
        auto result = Loader::LoadFromMemory(junk.data(), junk.size());
        CHECK(result != Loader::LoadResult::Ok); // junk is not a valid ELF
    }

    Memory::Shutdown();
    Loader::Shutdown();
}

TEST_CASE("Phase8::Fuzz::Loader::TruncatedELFHeader", "[fuzz][phase8]")
{
    Loader::Init();
    Memory::Init();

    // Starts with ELF magic but is truncated
    static const uint8_t truncated[] = {0x7F,'E','L','F', 0x02, 0x01, 0x01};
    auto r = Loader::LoadFromMemory(truncated, sizeof(truncated));
    CHECK(r == Loader::LoadResult::InvalidElf);

    Memory::Shutdown();
    Loader::Shutdown();
}

TEST_CASE("Phase8::Fuzz::Loader::EmptyBufferReturnsError", "[fuzz][phase8]")
{
    Loader::Init();
    Memory::Init();

    auto r = Loader::LoadFromMemory(nullptr, 0);
    CHECK(r != Loader::LoadResult::Ok);

    Memory::Shutdown();
    Loader::Shutdown();
}

TEST_CASE("Phase8::Fuzz::Loader::CorruptedSectionHeaders", "[fuzz][phase8]")
{
    Loader::Init();
    Memory::Init();

    // Valid ELF64 magic + class/data/version + rest junk
    std::vector<uint8_t> elf(256, 0);
    elf[0] = 0x7F; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 2;    // 64-bit
    elf[5] = 1;    // little-endian
    elf[6] = 1;    // ELF version
    // Corrupt everything else
    std::mt19937 rng(42);
    for (size_t i = 7; i < elf.size(); ++i) elf[i] = static_cast<uint8_t>(rng() & 0xFF);

    auto r = Loader::LoadFromMemory(elf.data(), elf.size());
    CHECK(r != Loader::LoadResult::Ok);

    Memory::Shutdown();
    Loader::Shutdown();
}

TEST_CASE("Phase8::Fuzz::Loader::NullPathReturnsError", "[fuzz][phase8]")
{
    Loader::Init();
    auto r = Loader::LoadFromPath("");
    CHECK(r != Loader::LoadResult::Ok);
    Loader::Shutdown();
}

TEST_CASE("Phase8::Fuzz::Loader::NonexistentPathReturnsError", "[fuzz][phase8]")
{
    Loader::Init();
    auto r = Loader::LoadFromPath("/no/such/file/here.elf");
    CHECK(r == Loader::LoadResult::FileNotFound);
    Loader::Shutdown();
}

// ── CPU decoder fuzz ──────────────────────────────────────────────────────

TEST_CASE("Phase8::Fuzz::Cpu::RandomOpcodesDoNotCrash", "[fuzz][phase8]")
{
    // Run random single bytes through Decode() — must never crash
    for (int byte = 0x00; byte <= 0xFF; ++byte) {
        uint8_t code[2] = { static_cast<uint8_t>(byte), 0x90 };
        // Decode is read-only and should handle any byte gracefully
        auto result = Cpu::Decode(code, 2);
        // result may be nullopt for unrecognised; that's fine
        (void)result;
    }
    CHECK(true); // reached = no crash
}

TEST_CASE("Phase8::Fuzz::Cpu::RandomBytesStep", "[fuzz][phase8]")
{
    Cpu::Init();
    // Feed random byte sequences through Step() — must not segfault.
    // Use stack-allocated buffers so addresses are valid.
    std::mt19937 rng(12345);
    for (int trial = 0; trial < 50; ++trial) {
        alignas(16) uint8_t code[16];
        for (auto& b : code) b = static_cast<uint8_t>(rng() & 0xFF);
        code[15] = 0xF4; // HLT as safety terminator

        Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code);
        auto r = Cpu::Step();
        // Any StepResult is acceptable — we just must not crash
        (void)r;
        Cpu::Reset();
    }
    Cpu::Shutdown();
}

TEST_CASE("Phase8::Fuzz::Cpu::REXPrefixAllValues", "[fuzz][phase8]")
{
    Cpu::Init();
    alignas(16) uint8_t code[3];
    // REX.W + NOP should always be safe (0x48 0x90)
    for (uint8_t rex = 0x40; rex <= 0x4F; ++rex) {
        code[0] = rex;
        code[1] = 0x90; // NOP
        code[2] = 0xF4; // HLT
        Cpu::GetContext().rip = reinterpret_cast<uint64_t>(code);
        auto r = Cpu::Step();
        // REX + NOP = Ok or Unimplemented, never a crash
        CHECK((r == Cpu::StepResult::Ok || r == Cpu::StepResult::Unimplemented));
        Cpu::Reset();
    }
    Cpu::Shutdown();
}

// ── CommandProcessor fuzz ─────────────────────────────────────────────────

TEST_CASE("Phase8::Fuzz::CommandProcessor::EmptyCommandList", "[fuzz][phase8]")
{
    GPU::Init(nullptr);
    CommandProcessor::Init(nullptr);

    for (int i = 0; i < 20; ++i) {
        CommandList cl;
        cl.End();
        CommandProcessor::Process(cl); // must not crash
    }

    CommandProcessor::Shutdown();
    GPU::Shutdown();
}

TEST_CASE("Phase8::Fuzz::CommandProcessor::MixedCommands", "[fuzz][phase8]")
{
    GPU::Init(nullptr);
    CommandProcessor::Init(nullptr);

    std::mt19937 rng(999);
    for (int trial = 0; trial < 20; ++trial) {
        CommandList cl;
        int ops = (rng() % 5) + 1;
        for (int i = 0; i < ops; ++i) {
            switch (rng() % 5) {
                case 0: cl.BeginRenderPass(); break;
                case 1: cl.ClearColor(0,0,0,1); break;
                case 2: cl.DrawDirect(3,1,0,0); break;
                case 3: cl.EndRenderPass(); break;
                case 4: cl.BarrierTransition(0,0,1); break;
            }
        }
        cl.End();
        CommandProcessor::Process(cl); // must not crash
    }

    CommandProcessor::Shutdown();
    GPU::Shutdown();
}

// ── Memory fuzz ───────────────────────────────────────────────────────────

TEST_CASE("Phase8::Fuzz::Memory::AllocZeroSize", "[fuzz][phase8]")
{
    Memory::Init();
    // Allocating zero bytes: implementation-defined but must not crash
    void* p = Memory::AllocHost(0, Memory::AllocType::Heap);
    if (p) Memory::FreeHost(p); // if returned, must be free-able
    CHECK(true);
    Memory::Shutdown();
}

TEST_CASE("Phase8::Fuzz::Memory::FreeInvalidPointerSafe", "[fuzz][phase8]")
{
    Memory::Init();
    // Freeing nullptr is documented as a no-op
    Memory::FreeHost(nullptr);
    CHECK(true);
    Memory::Shutdown();
}

TEST_CASE("Phase8::Fuzz::Memory::VaryingSizes", "[fuzz][phase8]")
{
    Memory::Init();
    std::vector<size_t> sizes = {1, 7, 16, 63, 128, 1023, 4096, 65536};
    for (size_t sz : sizes) {
        void* p = Memory::AllocHost(sz, Memory::AllocType::Heap);
        REQUIRE(p != nullptr);
        std::memset(p, 0xFF, sz); // touch all bytes
        Memory::FreeHost(p);
    }
    Memory::Shutdown();
}

// ── Filesystem path fuzz ──────────────────────────────────────────────────

TEST_CASE("Phase8::Fuzz::Filesystem::MalformedPathsDoNotCrash", "[fuzz][phase8]")
{
    Filesystem::Init();
    std::vector<std::string> badPaths = {
        "",
        "/",
        "///",
        "/app0/",
        "/../../etc/passwd",
        "/app0/\x00\xFF",
        std::string(1024, 'a'),
        "/app0/" + std::string(300, '/'),
    };
    for (auto& p : badPaths) {
        auto resolved = Filesystem::Resolve(p);
        (void)resolved; // must not crash
    }
    CHECK(true);
    Filesystem::Shutdown();
}
