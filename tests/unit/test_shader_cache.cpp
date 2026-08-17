// PS5x – Shader Cache tests (Phase 5)
// SPDX-License-Identifier: MIT
#include "PS5x/Logger/Logger.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"
#include "PS5x/ShaderCache/ShaderCache.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstring>
#include <filesystem>
#include <thread>

using namespace PS5x::ShaderCache;
namespace fs = std::filesystem;

static ShaderKey MakeKey(uint64_t spirvH, uint64_t pipeH, ShaderStage stage) {
  ShaderKey k;
  k.spirvHash = spirvH;
  k.pipelineHash = pipeH;
  k.stage = stage;
  return k;
}

static std::vector<uint8_t> FakeSPIRV(uint8_t seed) {
  return {0x03, 0x02, 0x23, 0x07, seed, 0x00, 0x01, 0x00};
}

static void Setup(const fs::path &dir = "") {
  PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
  PS5x::RuntimeEvents::Init();
  Init(dir, 64);
}
static void Teardown() {
  Shutdown();
  PS5x::RuntimeEvents::Shutdown();
  PS5x::Logger::Shutdown();
}

// ── Name ─────────────────────────────────────────────────────────────────
TEST_CASE("ShaderCache – StageName coverage", "[sc]") {
  REQUIRE(std::string(StageName(ShaderStage::Vertex)) == "Vertex");
  REQUIRE(std::string(StageName(ShaderStage::Fragment)) == "Fragment");
  REQUIRE(std::string(StageName(ShaderStage::Compute)) == "Compute");
}

// ── Hash ─────────────────────────────────────────────────────────────────
TEST_CASE("ShaderCache – HashSpirv is deterministic", "[sc]") {
  Setup();
  auto spirv = FakeSPIRV(0xAB);
  auto h1 = HashSpirv(spirv.data(), spirv.size());
  auto h2 = HashSpirv(spirv.data(), spirv.size());
  REQUIRE(h1 == h2);
  REQUIRE(h1 != 0);
  Teardown();
}

TEST_CASE("ShaderCache – Different SPIR-V produces different hash", "[sc]") {
  Setup();
  auto a = FakeSPIRV(0x01);
  auto b = FakeSPIRV(0x02);
  REQUIRE(HashSpirv(a.data(), a.size()) != HashSpirv(b.data(), b.size()));
  Teardown();
}

// ── Lookup ────────────────────────────────────────────────────────────────
TEST_CASE("ShaderCache – Lookup miss on empty cache", "[sc]") {
  Setup();
  auto key = MakeKey(1, 2, ShaderStage::Vertex);
  REQUIRE(!Lookup(key).has_value());
  auto s = GetStats();
  REQUIRE(s.misses == 1);
  Teardown();
}

// ── Compile ───────────────────────────────────────────────────────────────
TEST_CASE("ShaderCache – Compile without custom compiler stores SPIR-V",
          "[sc]") {
  Setup();
  auto spirv = FakeSPIRV(0x10);
  auto key =
      MakeKey(HashSpirv(spirv.data(), spirv.size()), 0, ShaderStage::Vertex);
  auto entry = Compile(key, spirv, "test-vert");
  REQUIRE(entry.has_value());
  REQUIRE(entry->valid);
  REQUIRE(!entry->binary.empty());
  REQUIRE(entry->debugName == "test-vert");
  Teardown();
}

TEST_CASE("ShaderCache – Compile with custom compiler uses it", "[sc]") {
  Setup();
  bool compilerCalled = false;
  SetCompiler([&](const std::vector<uint8_t> &spirv,
                  ShaderStage) -> std::vector<uint8_t> {
    compilerCalled = true;
    // Produce a 'compiled' binary: reverse the SPIR-V
    auto out = spirv;
    std::reverse(out.begin(), out.end());
    return out;
  });

  auto spirv = FakeSPIRV(0x20);
  auto key =
      MakeKey(HashSpirv(spirv.data(), spirv.size()), 1, ShaderStage::Fragment);
  auto entry = Compile(key, spirv, "custom-frag");
  REQUIRE(entry.has_value());
  REQUIRE(compilerCalled);
  // Binary should be the reversed SPIR-V
  auto expected = spirv;
  std::reverse(expected.begin(), expected.end());
  REQUIRE(entry->binary == expected);
  Teardown();
}

TEST_CASE("ShaderCache – Second Compile returns cache hit", "[sc]") {
  Setup();
  auto spirv = FakeSPIRV(0x30);
  auto key =
      MakeKey(HashSpirv(spirv.data(), spirv.size()), 2, ShaderStage::Compute);
  Compile(key, spirv, "compute");
  Compile(key, spirv, "compute"); // second call = cache hit
  auto s = GetStats();
  REQUIRE(s.hits >= 1);
  REQUIRE(s.compilations == 1); // only compiled once
  Teardown();
}

// ── Lookup after compile ───────────────────────────────────────────────────
TEST_CASE("ShaderCache – Lookup succeeds after Compile", "[sc]") {
  Setup();
  auto spirv = FakeSPIRV(0x40);
  auto key =
      MakeKey(HashSpirv(spirv.data(), spirv.size()), 3, ShaderStage::Vertex);
  Compile(key, spirv, "post-compile-lookup");
  auto entry = Lookup(key);
  REQUIRE(entry.has_value());
  REQUIRE(entry->hitCount >= 1);
  Teardown();
}

// ── Invalidate ────────────────────────────────────────────────────────────
TEST_CASE("ShaderCache – Invalidate removes specific entry", "[sc]") {
  Setup();
  auto spirv = FakeSPIRV(0x50);
  auto key =
      MakeKey(HashSpirv(spirv.data(), spirv.size()), 4, ShaderStage::Fragment);
  Compile(key, spirv, "to-invalidate");
  REQUIRE(Lookup(key).has_value());

  REQUIRE(Invalidate(key));
  REQUIRE(!Lookup(key).has_value());
  Teardown();
}

TEST_CASE("ShaderCache – InvalidateAll clears cache", "[sc]") {
  Setup();
  for (uint8_t i = 0; i < 5; ++i) {
    auto spirv = FakeSPIRV(i);
    auto key =
        MakeKey(HashSpirv(spirv.data(), spirv.size()), i, ShaderStage::Vertex);
    Compile(key, spirv);
  }
  REQUIRE(GetStats().entries == 5);
  REQUIRE(InvalidateAll() == 5);
  REQUIRE(GetStats().entries == 0);
  Teardown();
}

// ── Background compilation ────────────────────────────────────────────────
TEST_CASE("ShaderCache – QueueCompile and FlushQueue", "[sc]") {
  Setup();
  int compiled = 0;
  SetCompiler(
      [&](const std::vector<uint8_t> &s, ShaderStage) -> std::vector<uint8_t> {
        compiled++;
        return s;
      });

  for (uint8_t i = 0; i < 4; ++i) {
    auto spirv = FakeSPIRV(i + 0x60);
    auto key = MakeKey(HashSpirv(spirv.data(), spirv.size()), i + 10,
                       ShaderStage::Vertex);
    QueueCompile(key, spirv, "bg-" + std::to_string(i));
  }
  FlushQueue();
  REQUIRE(PendingCount() == 0);
  REQUIRE(compiled == 4);
  Teardown();
}

// ── Eviction ─────────────────────────────────────────────────────────────
TEST_CASE("ShaderCache – Evict removes stale entries", "[sc]") {
  Setup();
  auto spirv = FakeSPIRV(0x70);
  auto key =
      MakeKey(HashSpirv(spirv.data(), spirv.size()), 99, ShaderStage::Vertex);
  Compile(key, spirv, "old-shader");

  // Sleep past the eviction window
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  uint32_t removed = Evict(1'000); // entries older than 1ms
  REQUIRE(removed >= 1);
  REQUIRE(!Lookup(key).has_value());
  Teardown();
}

// ── Stats ─────────────────────────────────────────────────────────────────
TEST_CASE("ShaderCache – GetStats and DumpStats don't crash", "[sc]") {
  Setup();
  auto s = GetStats();
  REQUIRE(s.entries == 0);
  REQUIRE(s.hits == 0);
  REQUIRE(s.avgCompileMs >= 0.0);
  DumpStats();
  Teardown();
}

// ── Disk persistence ──────────────────────────────────────────────────────
TEST_CASE("ShaderCache – SaveToDisk / LoadFromDisk roundtrip", "[sc]") {
  auto dir = fs::temp_directory_path() / "ps5x_sc_test";
  fs::create_directories(dir);
  auto cachePath = dir / "shadercache.bin";

  // Write
  {
    Setup(dir);
    auto spirv = FakeSPIRV(0x80);
    auto key = MakeKey(HashSpirv(spirv.data(), spirv.size()), 200,
                       ShaderStage::Compute);
    Compile(key, spirv, "persist-me");
    REQUIRE(SaveToDisk(cachePath));
    Teardown();
  }

  // Read back
  {
    Setup();
    REQUIRE(LoadFromDisk(cachePath));
    REQUIRE(GetStats().entries >= 1);
    Teardown();
  }

  fs::remove_all(dir);
}
