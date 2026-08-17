// PS5x – Phase 6 Filesystem tests (tracing, temp FS, read-only query)
// SPDX-License-Identifier: MIT
#include "PS5x/Filesystem/Filesystem.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

using namespace PS5x::Filesystem;

// ── Tracing tests ──────────────────────────────────────────────────────────

TEST_CASE("Phase6::FS::Trace::EnableDisable", "[filesystem][phase6]") {
  EnableTracing(false);
  CHECK_FALSE(IsTracingEnabled());
  EnableTracing(true);
  CHECK(IsTracingEnabled());
  EnableTracing(false);
}

TEST_CASE("Phase6::FS::Trace::ClearWorks", "[filesystem][phase6]") {
  EnableTracing(true);
  // Trigger an operation to populate trace
  CreateTempFile("trace_clear_test");
  ClearTrace();
  auto entries = GetTrace();
  CHECK(entries.empty());
  EnableTracing(false);
}

TEST_CASE("Phase6::FS::Trace::EventsRecorded", "[filesystem][phase6]") {
  ClearTrace();
  EnableTracing(true);
  CreateTempFile("trace_event_test");
  auto entries = GetTrace();
  CHECK(!entries.empty());
  bool foundOpen = false;
  for (auto &e : entries) {
    if (e.event == FsEvent::Open)
      foundOpen = true;
  }
  CHECK(foundOpen);
  EnableTracing(false);
}

TEST_CASE("Phase6::FS::Trace::NotRecordedWhenDisabled",
          "[filesystem][phase6]") {
  EnableTracing(false);
  ClearTrace();
  CreateTempFile("no_trace_test");
  auto entries = GetTrace();
  CHECK(entries.empty());
}

TEST_CASE("Phase6::FS::Trace::MaxEntriesRespected", "[filesystem][phase6]") {
  EnableTracing(true);
  ClearTrace();
  for (int i = 0; i < 20; ++i) {
    CreateTempFile("bulk_" + std::to_string(i));
  }
  auto all = GetTrace();
  auto limited = GetTrace(5);
  CHECK(limited.size() <= 5);
  CHECK(all.size() >= limited.size());
  EnableTracing(false);
}

TEST_CASE("Phase6::FS::Trace::TimestampsNonZero", "[filesystem][phase6]") {
  EnableTracing(true);
  ClearTrace();
  CreateTempFile("ts_check");
  auto entries = GetTrace();
  for (auto &e : entries) {
    CHECK(e.timestampUs > 0);
  }
  EnableTracing(false);
}

// ── FsEventName tests ──────────────────────────────────────────────────────

TEST_CASE("Phase6::FS::EventName::AllNames", "[filesystem][phase6]") {
  CHECK(std::string(FsEventName(FsEvent::Open)) == "Open");
  CHECK(std::string(FsEventName(FsEvent::Close)) == "Close");
  CHECK(std::string(FsEventName(FsEvent::Read)) == "Read");
  CHECK(std::string(FsEventName(FsEvent::Write)) == "Write");
  CHECK(std::string(FsEventName(FsEvent::Stat)) == "Stat");
  CHECK(std::string(FsEventName(FsEvent::MkDir)) == "MkDir");
  CHECK(std::string(FsEventName(FsEvent::Remove)) == "Remove");
  CHECK(std::string(FsEventName(FsEvent::Rename)) == "Rename");
  CHECK(std::string(FsEventName(FsEvent::Mount)) == "Mount");
}

// ── TempFile tests ─────────────────────────────────────────────────────────

TEST_CASE("Phase6::FS::TempFile::ReturnsGuestPath", "[filesystem][phase6]") {
  std::string path = CreateTempFile("mytemp");
  CHECK(!path.empty());
  CHECK(path.find("/temp/") != std::string::npos);
  CHECK(path.find("mytemp") != std::string::npos);
}

TEST_CASE("Phase6::FS::TempFile::UnnamedGetsUniqueNames",
          "[filesystem][phase6]") {
  std::string p1 = CreateTempFile();
  std::string p2 = CreateTempFile();
  CHECK(p1 != p2);
  CHECK(!p1.empty());
  CHECK(!p2.empty());
}

TEST_CASE("Phase6::FS::TempFile::NamedIsConsistent", "[filesystem][phase6]") {
  std::string p = CreateTempFile("consistent_name");
  CHECK(p == "/temp/consistent_name");
}

TEST_CASE("Phase6::FS::TempFile::MultipleCallsDoNotCrash",
          "[filesystem][phase6]") {
  for (int i = 0; i < 10; ++i) {
    auto p = CreateTempFile();
    CHECK(!p.empty());
  }
}

// ── ReadOnly query tests ───────────────────────────────────────────────────

TEST_CASE("Phase6::FS::ReadOnly::App0DefaultNotReadOnly",
          "[filesystem][phase6]") {
  Init();
  // Unmounted App0 defaults to readOnly=false (MountEntry default)
  bool ro = IsReadOnly(MountPoint::App0);
  CHECK_FALSE(ro); // default MountEntry has readOnly=false
  Shutdown();
}

TEST_CASE("Phase6::FS::ReadOnly::SaveDataDefaultWritable",
          "[filesystem][phase6]") {
  Init();
  bool ro = IsReadOnly(MountPoint::SaveData);
  CHECK_FALSE(ro);
  Shutdown();
}

TEST_CASE("Phase6::FS::ReadOnly::InvalidMountReturnTrue",
          "[filesystem][phase6]") {
  // Out-of-range mount should safely return true (conservative)
  bool ro = IsReadOnly(static_cast<MountPoint>(255));
  CHECK(ro);
}
