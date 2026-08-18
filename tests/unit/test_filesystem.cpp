// PS5x – Filesystem unit tests (Phase 2)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/Filesystem/Filesystem.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace PS5x::Filesystem;

static fs::path TmpDir(const char* sub)
{
    auto d = fs::temp_directory_path() / "ps5x_fs_test" / sub;
    fs::create_directories(d);
    return d;
}

static void Setup()   { PS5x::Logger::Init("",false,PS5x::Logger::Level::Off); Init(); }
static void Teardown(){ Shutdown(); PS5x::Logger::Shutdown(); }

// ── Mount management ──────────────────────────────────────────────────────

TEST_CASE("FS – Mount and IsMounted", "[fs]")
{
    Setup();
    auto d = TmpDir("mount");
    REQUIRE( Mount(MountPoint::App0, d));
    REQUIRE( IsMounted(MountPoint::App0));
    REQUIRE(!IsMounted(MountPoint::SaveData));
    REQUIRE( Unmount(MountPoint::App0));
    REQUIRE(!IsMounted(MountPoint::App0));
    Teardown();
}

TEST_CASE("FS – GetHostPath returns correct path", "[fs]")
{
    Setup();
    auto d = TmpDir("hostpath");
    Mount(MountPoint::User, d);
    auto h = GetHostPath(MountPoint::User);
    REQUIRE(h.has_value());
    REQUIRE(*h == d);
    Teardown();
}

TEST_CASE("FS – DumpMounts does not crash", "[fs]")
{
    Setup();
    Mount(MountPoint::App0,    TmpDir("dump_app0"));
    Mount(MountPoint::SaveData,TmpDir("dump_save"));
    DumpMounts(); // must not crash
    Teardown();
}

// ── Path normalisation ─────────────────────────────────────────────────────

TEST_CASE("FS – Normalise collapses double slashes and dotdot", "[fs]")
{
    REQUIRE(Normalise("/app0//foo/../bar") == "/app0/bar");
    REQUIRE(Normalise("/app0/./eboot.bin") == "/app0/eboot.bin");
    REQUIRE(Normalise("//savedata/SAVES")  == "/savedata/SAVES");
}

// ── Resolve ────────────────────────────────────────────────────────────────

TEST_CASE("FS – Resolve maps guest path to host", "[fs]")
{
    Setup();
    auto d = TmpDir("resolve");
    // write a file on host
    { std::ofstream f(d / "hello.txt"); f << "hi"; }
    Mount(MountPoint::App0, d);
    auto r = Resolve("/app0/hello.txt");
    REQUIRE(r.has_value());
    REQUIRE(fs::exists(*r));
    Teardown();
}

TEST_CASE("FS – Resolve unmounted prefix returns nullopt", "[fs]")
{
    Setup();
    auto r = Resolve("/system/lib/libkernel.sprx");
    REQUIRE(!r.has_value());
    Teardown();
}

// ── File I/O ──────────────────────────────────────────────────────────────

TEST_CASE("FS – Open / Read / Seek / Tell / Size / Close", "[fs]")
{
    Setup();
    auto d = TmpDir("io");
    { std::ofstream f(d/"data.bin"); f << "ABCDE"; }
    Mount(MountPoint::App0, d);

    auto fd = Open("/app0/data.bin", OpenFlags::Read);
    REQUIRE(fd != INVALID_FD);
    REQUIRE(Size(fd) == 5);

    char buf[8]{};
    REQUIRE(Read(fd, buf, 5) == 5);
    REQUIRE(std::string(buf, 5) == "ABCDE");
    REQUIRE(Tell(fd) == 5);

    REQUIRE(Seek(fd, 2, SeekOrigin::Set) == 2);
    char b2[4]{};
    REQUIRE(Read(fd, b2, 3) == 3);
    REQUIRE(std::string(b2, 3) == "CDE");

    REQUIRE(Close(fd));
    Teardown();
}

TEST_CASE("FS – Write then Read back", "[fs]")
{
    Setup();
    auto d = TmpDir("write");
    Mount(MountPoint::SaveData, d);

    auto wfd = Open("/savedata/test.sav", OpenFlags::Write | OpenFlags::Create | OpenFlags::Truncate);
    REQUIRE(wfd != INVALID_FD);
    const char* msg = "PS5xSAVE";
    REQUIRE(Write(wfd, msg, 8) == 8);
    REQUIRE(Flush(wfd));
    REQUIRE(Close(wfd));

    auto rfd = Open("/savedata/test.sav", OpenFlags::Read);
    REQUIRE(rfd != INVALID_FD);
    char rb[16]{};
    REQUIRE(Read(rfd, rb, 8) == 8);
    REQUIRE(std::string(rb, 8) == "PS5xSAVE");
    Close(rfd);
    Teardown();
}

TEST_CASE("FS – Write denied on read-only mount", "[fs]")
{
    Setup();
    auto d = TmpDir("readonly");
    { std::ofstream f(d/"x.bin"); f << "x"; }
    Mount(MountPoint::App0, d, /*readOnly=*/true);
    auto fd = Open("/app0/x.bin", OpenFlags::Write);
    REQUIRE(fd == INVALID_FD);
    Teardown();
}

// ── Directory I/O ─────────────────────────────────────────────────────────

TEST_CASE("FS – Exists, MakeDir, ReadDir, Remove, Rename", "[fs]")
{
    Setup();
    auto d = TmpDir("dirops");
    std::filesystem::remove_all(d / "subdir");
    Mount(MountPoint::SaveData, d);

    REQUIRE(!Exists("/savedata/subdir"));
    REQUIRE(MakeDir("/savedata/subdir"));
    REQUIRE(Exists("/savedata/subdir"));

    // Create a file inside
    auto fd = Open("/savedata/subdir/f.txt",
                   OpenFlags::Write | OpenFlags::Create);
    const char* c = "hi";
    Write(fd, c, 2);
    Close(fd);

    auto entries = ReadDir("/savedata/subdir");
    REQUIRE(!entries.empty());
    bool found = false;
    for (const auto& e : entries) if (e.name == "f.txt") found = true;
    REQUIRE(found);

    // Rename
    REQUIRE(Rename("/savedata/subdir/f.txt", "/savedata/subdir/g.txt"));
    REQUIRE(Exists("/savedata/subdir/g.txt"));
    REQUIRE(!Exists("/savedata/subdir/f.txt"));

    // Remove
    REQUIRE(Remove("/savedata/subdir/g.txt"));
    REQUIRE(!Exists("/savedata/subdir/g.txt"));

    Teardown();
}

TEST_CASE("FS – Stat on file and directory", "[fs]")
{
    Setup();
    auto d = TmpDir("stat");
    { std::ofstream f(d/"a.txt"); f << "hello"; }
    Mount(MountPoint::App0, d);

    auto sf = Stat("/app0/a.txt");
    REQUIRE(sf.exists);
    REQUIRE(!sf.isDir);
    REQUIRE(sf.size == 5);

    auto sd = Stat("/app0");
    REQUIRE(sd.exists);
    REQUIRE(sd.isDir);

    auto sn = Stat("/app0/nonexistent.bin");
    REQUIRE(!sn.exists);
    Teardown();
}

// ── Save data helper ──────────────────────────────────────────────────────

TEST_CASE("FS – EnsureSaveDir creates directory", "[fs]")
{
    Setup();
    auto d = TmpDir("savehelper");
    Mount(MountPoint::SaveData, d);

    auto path = EnsureSaveDir("CUSA12345");
    REQUIRE(path == "/savedata/CUSA12345");
    REQUIRE(Exists(path));
    Teardown();
}
