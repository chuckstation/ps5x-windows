// ChuckStation5 – Dynamic Linker tests (Phase 5)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/Loader/Loader.h"
#include "ChuckStation5/ModuleRegistry/ModuleRegistry.h"
#include "ChuckStation5/RuntimeEvents/RuntimeEvents.h"
#include "ChuckStation5/DynamicLinker/DynamicLinker.h"

using namespace ChuckStation5::DynamicLinker;
using namespace ChuckStation5::ModuleRegistry;

static void Setup()
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();
    ChuckStation5::Loader::Init();
    ChuckStation5::RuntimeEvents::Init();
    ChuckStation5::ModuleRegistry::Init();
    ChuckStation5::DynamicLinker::Init();
}
static void Teardown()
{
    ChuckStation5::DynamicLinker::Shutdown();
    ChuckStation5::ModuleRegistry::Shutdown();
    ChuckStation5::RuntimeEvents::Shutdown();
    ChuckStation5::Loader::Shutdown();
    ChuckStation5::Memory::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

// ── RelocTypeName ─────────────────────────────────────────────────────────
TEST_CASE("DynLink – RelocTypeName coverage", "[dynlink]")
{
    REQUIRE(std::string(RelocTypeName(RelocType::None))     == "R_NONE");
    REQUIRE(std::string(RelocTypeName(RelocType::JumpSlot)) == "R_X86_64_JUMP_SLOT");
    REQUIRE(std::string(RelocTypeName(RelocType::Relative)) == "R_X86_64_RELATIVE");
    REQUIRE(std::string(RelocTypeName(RelocType::GlobDat))  == "R_X86_64_GLOB_DAT");
}

// ── Symbol cache ──────────────────────────────────────────────────────────
TEST_CASE("DynLink – Symbol cache miss on empty registry", "[dynlink]")
{
    Setup();
    auto sym = LookupSymbol("nonexistent_symbol");
    REQUIRE(!sym.has_value());
    auto s = GetStats();
    REQUIRE(s.cacheMisses >= 1);
    Teardown();
}

TEST_CASE("DynLink – Symbol resolves from ModuleRegistry and is cached", "[dynlink]")
{
    Setup();
    // Register a module with an exported symbol
    ChuckStation5::Loader::ExecutableInfo info;
    ChuckStation5::Loader::Symbol sym;
    sym.name="sceKernelOpen"; sym.value=0xABCD; sym.size=64; sym.type=2;
    info.symbols.push_back(sym);
    info.imageBase = 0; info.loaded = false;
    Register("libSceKernel", "/lib/libSceKernel", info);

    auto found = LookupSymbol("sceKernelOpen");
    REQUIRE(found.has_value());
    REQUIRE(found->address == 0xABCD);

    // Second lookup → cache hit
    auto found2 = LookupSymbol("sceKernelOpen");
    REQUIRE(found2.has_value());
    auto s = GetStats();
    REQUIRE(s.cacheHits >= 1);
    Teardown();
}

TEST_CASE("DynLink – LookupSymbolIn scoped to module", "[dynlink]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo infoA, infoB;
    ChuckStation5::Loader::Symbol sA; sA.name="foo"; sA.value=0x1000; sA.size=8; sA.type=2;
    ChuckStation5::Loader::Symbol sB; sB.name="foo"; sB.value=0x2000; sB.size=8; sB.type=2;
    infoA.symbols.push_back(sA);
    infoB.symbols.push_back(sB);
    infoA.imageBase=0; infoA.loaded=false;
    infoB.imageBase=0; infoB.loaded=false;
    auto idA = Register("modA", "/modA", infoA);
    auto idB = Register("modB", "/modB", infoB);

    auto fromA = LookupSymbolIn(idA, "foo");
    auto fromB = LookupSymbolIn(idB, "foo");
    REQUIRE(fromA.has_value());
    REQUIRE(fromB.has_value());
    REQUIRE(fromA->address == 0x1000);
    REQUIRE(fromB->address == 0x2000);
    Teardown();
}

TEST_CASE("DynLink – InvalidateCache removes entries for module", "[dynlink]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    ChuckStation5::Loader::Symbol sym; sym.name="bar"; sym.value=0x5000; sym.type=2;
    info.symbols.push_back(sym);
    info.loaded=false;
    auto id = Register("libbar", "/libbar", info);

    LookupSymbol("bar"); // populate cache
    REQUIRE(GetStats().cacheEntries >= 1);

    InvalidateCache(id);
    // After invalidation, a lookup is a cache miss again
    auto s = GetStats();
    auto missBefore = s.cacheMisses;
    LookupSymbol("bar"); // cache miss then re-populate
    REQUIRE(GetStats().cacheMisses > missBefore);
    Teardown();
}

TEST_CASE("DynLink – ClearCache empties everything", "[dynlink]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    ChuckStation5::Loader::Symbol sym; sym.name="baz"; sym.value=0x9000; sym.type=2;
    info.symbols.push_back(sym);
    info.loaded=false;
    Register("libbaz","/libbaz",info);
    LookupSymbol("baz");
    REQUIRE(GetStats().cacheEntries >= 1);
    ClearCache();
    REQUIRE(GetStats().cacheEntries == 0);
    Teardown();
}

// ── Dependency graph ──────────────────────────────────────────────────────
TEST_CASE("DynLink – No cycles in simple chain", "[dynlink]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    info.loaded=false;
    Register("A","/A",info);
    Register("B","/B",info);
    auto cycles = DetectCycles();
    REQUIRE(!cycles.has_value());
    Teardown();
}

TEST_CASE("DynLink – TopologicalOrder returns all modules", "[dynlink]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    info.loaded=false;
    Register("X","/X",info);
    Register("Y","/Y",info);
    Register("Z","/Z",info);
    auto order = TopologicalOrder();
    REQUIRE(order.size() == 3);
    Teardown();
}

TEST_CASE("DynLink – CanUnload returns true when no dependents", "[dynlink]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info; info.loaded=false;
    auto id = Register("standalone","/standalone",info);
    REQUIRE(CanUnload(id));
    Teardown();
}

// ── Link operations ───────────────────────────────────────────────────────
TEST_CASE("DynLink – LinkAll with no modules returns zero", "[dynlink]")
{
    Setup();
    auto r = LinkAll();
    REQUIRE(r.resolved   == 0);
    REQUIRE(r.unresolved == 0);
    Teardown();
}

TEST_CASE("DynLink – DumpRelocations and DumpSymbolCache don't crash", "[dynlink]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info; info.loaded=false;
    auto id = Register("dump-test","/dump",info);
    DumpRelocations(id);
    DumpSymbolCache();
    LinkResult r; r.resolved=2; r.unresolved=1; r.missingSymbols={"missing"};
    DumpLinkResult(r);
    Teardown();
}

TEST_CASE("DynLink – GetStats reflects cache operations", "[dynlink]")
{
    Setup();
    auto s0 = GetStats();
    REQUIRE(s0.cacheHits   == 0);
    REQUIRE(s0.cacheMisses == 0);

    ChuckStation5::Loader::ExecutableInfo info; info.loaded=false;
    ChuckStation5::Loader::Symbol sym; sym.name="fn"; sym.value=0x100; sym.type=2;
    info.symbols.push_back(sym);
    Register("lib","/lib",info);

    LookupSymbol("fn");       // miss + populate
    LookupSymbol("fn");       // hit
    LookupSymbol("notfound"); // miss

    auto s1 = GetStats();
    REQUIRE(s1.cacheHits   >= 1);
    REQUIRE(s1.cacheMisses >= 2);
    Teardown();
}

TEST_CASE("DynLink – EagerBind with no jump-slot relocs returns 0", "[dynlink]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info; info.loaded=false;
    auto id = Register("eager","/eager",info);
    REQUIRE(EagerBind(id) == 0);
    REQUIRE(LazyCount()   == 0);
    Teardown();
}
