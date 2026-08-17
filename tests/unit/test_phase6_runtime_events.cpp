// PS5x – Phase 6 RuntimeEvents tests
// SPDX-License-Identifier: MIT
#include "PS5x/RuntimeEvents/RuntimeEvents.h"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace PS5x::RuntimeEvents;

// ── EventCategoryName ──────────────────────────────────────────────────────

TEST_CASE("Phase6::RE::CategoryName::AllNames", "[runtime_events][phase6]") {
  CHECK(std::string(EventCategoryName(EventCategory::Lifecycle)) ==
        "Lifecycle");
  CHECK(std::string(EventCategoryName(EventCategory::Thread)) == "Thread");
  CHECK(std::string(EventCategoryName(EventCategory::Module)) == "Module");
  CHECK(std::string(EventCategoryName(EventCategory::Memory)) == "Memory");
  CHECK(std::string(EventCategoryName(EventCategory::GPU)) == "GPU");
  CHECK(std::string(EventCategoryName(EventCategory::Audio)) == "Audio");
  CHECK(std::string(EventCategoryName(EventCategory::Filesystem)) ==
        "Filesystem");
  CHECK(std::string(EventCategoryName(EventCategory::Scheduler)) ==
        "Scheduler");
  CHECK(std::string(EventCategoryName(EventCategory::Syscall)) == "Syscall");
  CHECK(std::string(EventCategoryName(EventCategory::Profile)) == "Profile");
  CHECK(std::string(EventCategoryName(EventCategory::Custom)) == "Custom");
}

// ── GetEventCategory ───────────────────────────────────────────────────────

TEST_CASE("Phase6::RE::GetCategory::Process", "[runtime_events][phase6]") {
  CHECK(GetEventCategory(EventType::ProcessCreated) ==
        EventCategory::Lifecycle);
  CHECK(GetEventCategory(EventType::ProcessExited) == EventCategory::Lifecycle);
  CHECK(GetEventCategory(EventType::ProcessFaulted) ==
        EventCategory::Lifecycle);
}

TEST_CASE("Phase6::RE::GetCategory::Thread", "[runtime_events][phase6]") {
  CHECK(GetEventCategory(EventType::ThreadSpawned) == EventCategory::Thread);
  CHECK(GetEventCategory(EventType::ThreadExited) == EventCategory::Thread);
  CHECK(GetEventCategory(EventType::ThreadFaulted) == EventCategory::Thread);
}

TEST_CASE("Phase6::RE::GetCategory::GPU", "[runtime_events][phase6]") {
  CHECK(GetEventCategory(EventType::FrameBegin) == EventCategory::GPU);
  CHECK(GetEventCategory(EventType::FrameEnd) == EventCategory::GPU);
  CHECK(GetEventCategory(EventType::ShaderCompiled) == EventCategory::GPU);
}

TEST_CASE("Phase6::RE::GetCategory::Profile", "[runtime_events][phase6]") {
  CHECK(GetEventCategory(EventType::ProfileMark) == EventCategory::Profile);
  CHECK(GetEventCategory(EventType::ProfileBegin) == EventCategory::Profile);
  CHECK(GetEventCategory(EventType::ProfileEnd) == EventCategory::Profile);
}

// ── SubscribeCategory ──────────────────────────────────────────────────────

TEST_CASE("Phase6::RE::SubscribeCategory::ThreadEvents",
          "[runtime_events][phase6]") {
  Init();
  Reset();
  std::atomic<int> count{0};
  auto id = SubscribeCategory(
      EventCategory::Thread, [&](const RuntimeEvent &) { count.fetch_add(1); });
  PublishThread(EventType::ThreadSpawned, 42, "worker");
  PublishThread(EventType::ThreadExited, 42, "worker");
  // Non-thread event - should NOT fire
  PublishFrame(0, 0.0, 0.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  CHECK(count.load() == 2);
  Unsubscribe(id);
  Shutdown();
}

TEST_CASE("Phase6::RE::SubscribeCategory::GPUEvents",
          "[runtime_events][phase6]") {
  Init();
  Reset();
  std::atomic<int> count{0};
  auto id = SubscribeCategory(
      EventCategory::GPU, [&](const RuntimeEvent &) { count.fetch_add(1); });
  PublishGpuEvent(EventType::FrameBegin, 1, 0.0, 0.0);
  PublishGpuEvent(EventType::FrameEnd, 1, 5.0, 1.0);
  // Thread event - should NOT fire for GPU subscriber
  PublishThread(EventType::ThreadSpawned, 1, "t");
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  CHECK(count.load() == 2);
  Unsubscribe(id);
  Shutdown();
}

// ── New publish helpers ────────────────────────────────────────────────────

TEST_CASE("Phase6::RE::PublishThread::AppearsInRecent",
          "[runtime_events][phase6]") {
  Init();
  Reset();
  PublishThread(EventType::ThreadSpawned, 100, "render_thread");
  auto recent = GetRecent(10);
  bool found = false;
  for (auto &e : recent) {
    if (e.type == EventType::ThreadSpawned) {
      auto *p = std::get_if<ThreadPayload>(&e.payload);
      if (p && p->threadId == 100)
        found = true;
    }
  }
  CHECK(found);
  Shutdown();
}

TEST_CASE("Phase6::RE::PublishGpu::AppearsInRecent",
          "[runtime_events][phase6]") {
  Init();
  Reset();
  PublishGpuEvent(EventType::FrameEnd, 77, 12.5, 3.2);
  auto recent = GetRecent(10);
  bool found = false;
  for (auto &e : recent) {
    if (e.type == EventType::FrameEnd) {
      auto *p = std::get_if<FramePayload>(&e.payload);
      if (p && p->frameIndex == 77)
        found = true;
    }
  }
  CHECK(found);
  Shutdown();
}

TEST_CASE("Phase6::RE::PublishAudio::AppearsInRecent",
          "[runtime_events][phase6]") {
  Init();
  Reset();
  PublishAudioEvent("underrun", "port=2 total=5");
  auto recent = GetRecent(10);
  bool found = false;
  for (auto &e : recent) {
    if (e.type == EventType::Custom) {
      auto *p = std::get_if<CustomPayload>(&e.payload);
      if (p && p->tag.find("audio") != std::string::npos)
        found = true;
    }
  }
  CHECK(found);
  Shutdown();
}

TEST_CASE("Phase6::RE::PublishFilesystem::AppearsInRecent",
          "[runtime_events][phase6]") {
  Init();
  Reset();
  PublishFilesystemEvent("/app0/sce_module/libc.prx", true);
  auto recent = GetRecent(10);
  bool found = false;
  for (auto &e : recent) {
    if (e.type == EventType::Custom) {
      auto *p = std::get_if<CustomPayload>(&e.payload);
      if (p && p->tag == "fs")
        found = true;
    }
  }
  CHECK(found);
  Shutdown();
}

TEST_CASE("Phase6::RE::PublishScheduler::AppearsInRecent",
          "[runtime_events][phase6]") {
  Init();
  Reset();
  PublishSchedulerEvent(55, "preempted");
  auto recent = GetRecent(10);
  CHECK(!recent.empty());
  Shutdown();
}

// ── Category counters ──────────────────────────────────────────────────────

TEST_CASE("Phase6::RE::CategoryCount::ThreadIncremented",
          "[runtime_events][phase6]") {
  Init();
  Reset();
  uint64_t before = GetCategoryCount(EventCategory::Thread);
  PublishThread(EventType::ThreadSpawned, 1, "t");
  uint64_t after = GetCategoryCount(EventCategory::Thread);
  CHECK(after >= before + 1);
  Shutdown();
}

TEST_CASE("Phase6::RE::CategoryCount::AudioIncremented",
          "[runtime_events][phase6]") {
  Init();
  Reset();
  uint64_t before = GetCategoryCount(EventCategory::Audio);
  PublishAudioEvent("test", "data");
  uint64_t after = GetCategoryCount(EventCategory::Audio);
  CHECK(after >= before + 1);
  Shutdown();
}

// ── Timeline export ────────────────────────────────────────────────────────

TEST_CASE("Phase6::RE::Export::JsonNotEmpty", "[runtime_events][phase6]") {
  Init();
  PublishProfileBegin("test_scope");
  PublishProfileEnd("test_scope", 1000);
  std::string json = ExportTimelineJson();
  CHECK(!json.empty());
  CHECK(json.find('[') != std::string::npos);
  CHECK(json.find(']') != std::string::npos);
  Shutdown();
}

TEST_CASE("Phase6::RE::Export::CsvHasHeader", "[runtime_events][phase6]") {
  Init();
  PublishProfileBegin("csv_test");
  std::string csv = ExportTimelineCsv();
  CHECK(!csv.empty());
  CHECK(csv.find("timestamp_us") != std::string::npos);
  Shutdown();
}

TEST_CASE("Phase6::RE::Export::JsonEmptyTimeline", "[runtime_events][phase6]") {
  Init();
  Reset();
  std::string json = ExportTimelineJson();
  CHECK(json.find('[') != std::string::npos);
  Shutdown();
}
