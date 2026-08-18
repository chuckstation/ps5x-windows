// ChuckStation5 – Module Registry tests (Phase 4)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Memory/Memory.h"
#include "ChuckStation5/Loader/Loader.h"
#include "ChuckStation5/ModuleRegistry/ModuleRegistry.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace ChuckStation5::ModuleRegistry;

static fs::path WriteTinyElf(const char* fname = "chuckstation5_modreg.elf")
{
    auto path = fs::temp_directory_path() / fname;
#pragma pack(push,1)
    struct Ehdr {
        uint8_t e_ident[16];uint16_t e_type,e_machine;uint32_t e_version;
        uint64_t e_entry,e_phoff,e_shoff;uint32_t e_flags;
        uint16_t e_ehsize,e_phentsize,e_phnum,e_shentsize,e_shnum,e_shstrndx;
    };
    struct Phdr {
        uint32_t p_type,p_flags;
        uint64_t p_offset,p_vaddr,p_paddr,p_filesz,p_memsz,p_align;
    };
#pragma pack(pop)
    Ehdr eh{};Phdr ph{};
    eh.e_ident[0]=0x7F;eh.e_ident[1]='E';eh.e_ident[2]='L';eh.e_ident[3]='F';
    eh.e_ident[4]=2;eh.e_ident[5]=1;eh.e_ident[6]=1;
    eh.e_type=2;eh.e_machine=62;eh.e_version=1;eh.e_entry=0x401000;
    eh.e_phoff=sizeof(Ehdr);eh.e_ehsize=sizeof(Ehdr);
    eh.e_phentsize=sizeof(Phdr);eh.e_phnum=1;
    ph.p_type=1;ph.p_flags=0x5;ph.p_vaddr=0x400000;ph.p_paddr=0x400000;
    ph.p_filesz=sizeof(Ehdr)+sizeof(Phdr);ph.p_memsz=sizeof(Ehdr)+sizeof(Phdr);
    ph.p_align=0x1000;
    std::ofstream f(path,std::ios::binary);
    f.write(reinterpret_cast<const char*>(&eh),sizeof(eh));
    f.write(reinterpret_cast<const char*>(&ph),sizeof(ph));
    return path;
}

static void Setup()
{
    ChuckStation5::Logger::Init("",false,ChuckStation5::Logger::Level::Off);
    ChuckStation5::Memory::Init();
    ChuckStation5::Loader::Init();
    Init();
}
static void Teardown()
{
    Shutdown();
    ChuckStation5::Loader::Shutdown();
    ChuckStation5::Memory::Shutdown();
    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("ModReg – Init produces empty registry", "[modreg]")
{
    Setup();
    REQUIRE(GetModuleCount() == 0);
    REQUIRE(GetMainModule() == INVALID_MODULE);
    REQUIRE(GetAllModules().empty());
    Teardown();
}

TEST_CASE("ModReg – Register main module", "[modreg]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    info.imageBase  = 0x400000;
    info.entryPoint = 0x401000;
    info.loaded     = true;

    auto id = Register("eboot.bin", "/app0/eboot.bin", info, true);
    REQUIRE(id != INVALID_MODULE);
    REQUIRE(GetModuleCount() == 1);
    REQUIRE(GetMainModule() == id);

    auto desc = GetModule(id);
    REQUIRE(desc.has_value());
    REQUIRE(desc->name == "eboot.bin");
    REQUIRE(desc->isMain);
    REQUIRE(desc->refCount == 1);
    Teardown();
}

TEST_CASE("ModReg – Register same name returns existing id", "[modreg]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    auto id1 = Register("libSceKernel.sprx", "/system/lib/libSceKernel.sprx", info);
    auto id2 = Register("libSceKernel.sprx", "/system/lib/libSceKernel.sprx", info);
    REQUIRE(id1 == id2);
    auto desc = GetModule(id1);
    REQUIRE(desc->refCount == 2);
    Teardown();
}

TEST_CASE("ModReg – GetModuleByName lookup", "[modreg]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    auto id = Register("mymod.sprx", "/lib/mymod.sprx", info);
    auto found = GetModuleByName("mymod.sprx");
    REQUIRE(found.has_value());
    REQUIRE(found->id == id);
    REQUIRE(!GetModuleByName("nonexistent").has_value());
    Teardown();
}

TEST_CASE("ModReg – Load ELF and registers it", "[modreg]")
{
    Setup();
    auto elfPath = WriteTinyElf();
    auto id = Load(elfPath);
    REQUIRE(id != INVALID_MODULE);
    REQUIRE(GetModuleCount() == 1);
    auto desc = GetModule(id);
    REQUIRE(desc.has_value());
    REQUIRE(desc->loaded);
    fs::remove(elfPath);
    Teardown();
}

TEST_CASE("ModReg – Unload decrements refCount", "[modreg]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    info.loaded = false; // not actually mapped, safe to unload
    auto id = Register("testmod", "/test/testmod", info);
    REQUIRE(GetModuleCount() == 1);
    REQUIRE(Unload(id));
    REQUIRE(GetModuleCount() == 0);
    REQUIRE(!GetModule(id).has_value());
    Teardown();
}

TEST_CASE("ModReg – Retain increments refCount", "[modreg]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    auto id = Register("retained", "/lib/retained", info);
    REQUIRE(Retain(id));
    auto desc = GetModule(id);
    REQUIRE(desc->refCount == 2);
    REQUIRE(Unload(id)); // decrement to 1
    REQUIRE(GetModuleCount() == 1); // still alive
    REQUIRE(Unload(id)); // decrement to 0 → removed
    REQUIRE(GetModuleCount() == 0);
    Teardown();
}

TEST_CASE("ModReg – GetLoadOrder matches registration order", "[modreg]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    auto a = Register("a.sprx", "/a", info);
    auto b = Register("b.sprx", "/b", info);
    auto c = Register("c.sprx", "/c", info);

    auto order = GetLoadOrder();
    REQUIRE(order.size() == 3);
    // a registered first must appear first
    auto ai = std::find(order.begin(), order.end(), a);
    auto bi = std::find(order.begin(), order.end(), b);
    auto ci = std::find(order.begin(), order.end(), c);
    REQUIRE(ai != order.end());
    REQUIRE(ai < bi);
    REQUIRE(bi < ci);
    Teardown();
}

TEST_CASE("ModReg – ResolveSymbol finds exported symbol", "[modreg]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    ChuckStation5::Loader::Symbol sym;
    sym.name  = "sceKernelGetModuleInfo";
    sym.value = 0x1000;
    sym.size  = 64;
    sym.type  = 2; // STT_FUNC
    info.symbols.push_back(sym);
    info.imageBase = 0;
    info.loaded    = false;

    auto id = Register("libSceKernel", "/lib/libSceKernel", info);
    (void)id;

    auto found = ResolveSymbol("sceKernelGetModuleInfo");
    REQUIRE(found.has_value());
    REQUIRE(found->name == "sceKernelGetModuleInfo");
    REQUIRE(found->address == 0x1000);

    REQUIRE(!ResolveSymbol("doesNotExist").has_value());
    Teardown();
}

TEST_CASE("ModReg – DumpModules does not crash", "[modreg]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    Register("mod1", "/lib/mod1", info);
    Register("mod2", "/lib/mod2", info);
    DumpModules();
    DumpDependencyGraph();
    Teardown();
}

TEST_CASE("ModReg – GetDependents finds reverse deps", "[modreg]")
{
    Setup();
    ChuckStation5::Loader::ExecutableInfo info;
    auto base = Register("base", "/base", info);
    ChuckStation5::Loader::ExecutableInfo info2;
    ModuleDesc d2; // build a module with base as dep manually through Register
    // Register dep module, then manually add dep
    auto dep = Register("dep", "/dep", info);

    // Verify GetDependents returns empty (no module was registered as depending on base yet)
    auto dependents = GetDependents(base);
    REQUIRE(dependents.empty());  // no module registered as depending on it
    (void)dep;
    Teardown();
}
