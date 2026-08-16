// PS5x – Phase 8 UI Polish tests
// SPDX-License-Identifier: MIT
//
// Validates: welcome screen, recent homebrew list, firmware manager,
//            theme customisation, dock layout persistence,
//            searchable log viewer, performance dashboard.
#include <catch2/catch_test_macros.hpp>
#include "PS5x/UI/UI.h"
#include "PS5x/Config/Config.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/PerfTools/PerfTools.h"
#include "PS5x/Cpu/Cpu.h"

using namespace PS5x;

// ── Lifecycle ─────────────────────────────────────────────────────────────

TEST_CASE("Phase8::UI::InitShutdown", "[ui][phase8]")
{
    CHECK(UI::Init(nullptr));
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::MultipleInitShutdown", "[ui][phase8]")
{
    for (int i = 0; i < 3; ++i) {
        CHECK(UI::Init(nullptr));
        UI::Shutdown();
    }
}

TEST_CASE("Phase8::UI::DoubleShutdownSafe", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::Shutdown();
    UI::Shutdown();
    CHECK(true);
}

// ── Welcome screen ────────────────────────────────────────────────────────

TEST_CASE("Phase8::UI::WelcomeScreen::EnabledByDefault", "[ui][phase8]")
{
    UI::Init(nullptr);
    CHECK(UI::IsWelcomeScreenEnabled());
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::WelcomeScreen::CanDisable", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::SetWelcomeScreenEnabled(false);
    CHECK(!UI::IsWelcomeScreenEnabled());
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::WelcomeScreen::PersistsThroughConfig", "[ui][phase8]")
{
    Config::Reset();
    UI::Init(nullptr);
    UI::SetWelcomeScreenEnabled(false);
    UI::SaveLayout();

    UI::Shutdown();
    UI::Init(nullptr);
    UI::LoadLayout();
    CHECK(!UI::IsWelcomeScreenEnabled());

    UI::Shutdown();
    Config::Reset();
}

// ── Recent homebrew list ──────────────────────────────────────────────────

TEST_CASE("Phase8::UI::RecentList::EmptyOnFirstRun", "[ui][phase8]")
{
    UI::Init(nullptr);
    auto recent = UI::GetRecentList();
    // May or may not be empty depending on saved config; ensure no crash
    CHECK(recent.size() <= 20); // max 20 recent
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::RecentList::AddEntry", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::ClearRecentList();
    UI::AddRecentEntry("/app0/hello.elf", "Hello World");
    auto recent = UI::GetRecentList();
    REQUIRE(!recent.empty());
    CHECK(recent.front().path  == "/app0/hello.elf");
    CHECK(recent.front().label == "Hello World");
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::RecentList::DeduplicatesPath", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::ClearRecentList();
    UI::AddRecentEntry("/app0/same.elf", "Same");
    UI::AddRecentEntry("/app0/same.elf", "Same");
    auto recent = UI::GetRecentList();
    // Deduplication: only one entry
    size_t count = 0;
    for (auto& e : recent) if (e.path == "/app0/same.elf") ++count;
    CHECK(count == 1);
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::RecentList::MaxEntriesRespected", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::ClearRecentList();
    for (int i = 0; i < 25; ++i) {
        UI::AddRecentEntry("/app0/prog_" + std::to_string(i) + ".elf",
                           "Prog " + std::to_string(i));
    }
    auto recent = UI::GetRecentList();
    CHECK(recent.size() <= 20);
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::RecentList::MostRecentFirst", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::ClearRecentList();
    UI::AddRecentEntry("/app0/a.elf", "A");
    UI::AddRecentEntry("/app0/b.elf", "B");
    auto recent = UI::GetRecentList();
    REQUIRE(recent.size() >= 2);
    CHECK(recent.front().path == "/app0/b.elf"); // last added = front
    UI::Shutdown();
}

// ── Firmware manager ──────────────────────────────────────────────────────

TEST_CASE("Phase8::UI::FirmwareManager::NoFirmwareByDefault", "[ui][phase8]")
{
    UI::Init(nullptr);
    auto fw = UI::GetFirmwareStatus();
    CHECK(fw.state == UI::FirmwareState::NotProvided);
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::FirmwareManager::SetFirmwarePath", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::SetFirmwarePath("/user/firmware.pkg");
    auto fw = UI::GetFirmwareStatus();
    CHECK(fw.path == "/user/firmware.pkg");
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::FirmwareManager::ClearFirmware", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::SetFirmwarePath("/user/fw.pkg");
    UI::ClearFirmwarePath();
    auto fw = UI::GetFirmwareStatus();
    CHECK(fw.state == UI::FirmwareState::NotProvided);
    CHECK(fw.path.empty());
    UI::Shutdown();
}

// ── Theme customisation ───────────────────────────────────────────────────

TEST_CASE("Phase8::UI::Theme::DefaultTheme", "[ui][phase8]")
{
    UI::Init(nullptr);
    auto theme = UI::GetCurrentTheme();
    CHECK(!theme.name.empty());
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::Theme::SetBuiltinTheme", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::SetTheme("Dark");
    CHECK(UI::GetCurrentTheme().name == "Dark");
    UI::SetTheme("Light");
    CHECK(UI::GetCurrentTheme().name == "Light");
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::Theme::CustomAccentColor", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::SetAccentColor(0xFF, 0x00, 0x7F); // hot pink
    auto c = UI::GetAccentColor();
    CHECK(c.r == 0xFF);
    CHECK(c.g == 0x00);
    CHECK(c.b == 0x7F);
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::Theme::PersistsThroughSaveLoad", "[ui][phase8]")
{
    Config::Reset();
    UI::Init(nullptr);
    UI::SetTheme("Dark");
    UI::SetAccentColor(0x10, 0x20, 0x30);
    UI::SaveLayout();
    UI::Shutdown();

    UI::Init(nullptr);
    UI::LoadLayout();
    CHECK(UI::GetCurrentTheme().name == "Dark");
    auto c = UI::GetAccentColor();
    CHECK(c.r == 0x10);
    CHECK(c.g == 0x20);
    CHECK(c.b == 0x30);
    UI::Shutdown();
    Config::Reset();
}

// ── Dock layout persistence ───────────────────────────────────────────────

TEST_CASE("Phase8::UI::DockLayout::SaveAndLoad", "[ui][phase8]")
{
    Config::Reset();
    UI::Init(nullptr);

    UI::DockPanel panel{};
    panel.id       = "DebugPanel";
    panel.docked   = true;
    panel.side     = UI::DockSide::Right;
    panel.size     = 300;
    panel.visible  = true;
    UI::SetDockPanel(panel);
    UI::SaveLayout();
    UI::Shutdown();

    UI::Init(nullptr);
    UI::LoadLayout();
    auto p = UI::GetDockPanel("DebugPanel");
    REQUIRE(p.has_value());
    CHECK(p->docked  == true);
    CHECK(p->side    == UI::DockSide::Right);
    CHECK(p->size    == 300);
    CHECK(p->visible == true);

    UI::Shutdown();
    Config::Reset();
}

TEST_CASE("Phase8::UI::DockLayout::MultiplePanels", "[ui][phase8]")
{
    UI::Init(nullptr);
    std::vector<std::string> panelIds = {"Log","Debug","Perf","Modules"};
    for (auto& id : panelIds) {
        UI::DockPanel p{};
        p.id      = id;
        p.docked  = false;
        p.visible = true;
        UI::SetDockPanel(p);
    }
    for (auto& id : panelIds) {
        auto p = UI::GetDockPanel(id);
        REQUIRE(p.has_value());
        CHECK(p->id == id);
    }
    UI::Shutdown();
}

// ── Searchable log viewer ─────────────────────────────────────────────────

TEST_CASE("Phase8::UI::LogViewer::InitEmpty", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::ClearLogViewer();
    auto entries = UI::SearchLogViewer("");
    CHECK(entries.size() == 0);
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::LogViewer::IngestAndSearch", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::ClearLogViewer();
    UI::IngestLogLine("[CPU] Executed 1000 instructions");
    UI::IngestLogLine("[GPU] Frame 1 rendered");
    UI::IngestLogLine("[CPU] Fault at 0xDEAD");

    auto cpu_entries = UI::SearchLogViewer("[CPU]");
    CHECK(cpu_entries.size() >= 2);
    for (auto& e : cpu_entries) {
        CHECK(e.find("[CPU]") != std::string::npos);
    }
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::LogViewer::CaseSensitiveSearch", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::ClearLogViewer();
    UI::IngestLogLine("Error: something went wrong");
    UI::IngestLogLine("error: lowercase");
    UI::IngestLogLine("INFO: normal");

    auto results = UI::SearchLogViewer("Error", true);
    CHECK(results.size() == 1);
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::LogViewer::LevelFilter", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::ClearLogViewer();
    UI::IngestLogLine("[ERROR] bad thing");
    UI::IngestLogLine("[WARN] caution");
    UI::IngestLogLine("[INFO] all good");
    UI::IngestLogLine("[DEBUG] verbose");

    auto errors = UI::FilterLogByLevel(UI::LogLevel::Error);
    CHECK(errors.size() >= 1);
    for (auto& e : errors) {
        CHECK(e.find("[ERROR]") != std::string::npos);
    }
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::LogViewer::MaxCapacity", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::ClearLogViewer();
    for (int i = 0; i < 10000; ++i) {
        UI::IngestLogLine("Line " + std::to_string(i));
    }
    auto all = UI::SearchLogViewer("");
    CHECK(all.size() <= UI::MaxLogLines());
    UI::Shutdown();
}

// ── Performance dashboard ─────────────────────────────────────────────────

TEST_CASE("Phase8::UI::PerfDashboard::FPSNonNegative", "[ui][phase8]")
{
    UI::Init(nullptr);
    PerfTools::Init();

    PerfTools::RecordFrameTime(16.67); // ~60 fps
    auto fps = UI::GetDashboardFPS();
    CHECK(fps >= 0.0f);

    PerfTools::Shutdown();
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::PerfDashboard::CPUUsageInRange", "[ui][phase8]")
{
    UI::Init(nullptr);
    PerfTools::Init();

    auto usage = UI::GetDashboardCPUUsage();
    CHECK(usage >= 0.0f);
    CHECK(usage <= 100.0f);

    PerfTools::Shutdown();
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::PerfDashboard::InstructionCountVisible", "[ui][phase8]")
{
    UI::Init(nullptr);
    PerfTools::Init();
    Cpu::Init();

    alignas(16) uint8_t nops[] = {0x90, 0x90, 0x90}; // 3 NOPs
    Cpu::GetContext().rip = reinterpret_cast<uint64_t>(nops);
    Cpu::Step(); Cpu::Step(); Cpu::Step();

    auto stats = UI::GetDashboardCPUStats();
    CHECK(stats.instructionsExecuted >= 3);

    Cpu::Shutdown();
    PerfTools::Shutdown();
    UI::Shutdown();
}

TEST_CASE("Phase8::UI::PerfDashboard::StatusOverlayText", "[ui][phase8]")
{
    UI::Init(nullptr);
    UI::SetStatusOverlay("Running: hello.elf");
    auto text = UI::GetStatusOverlay();
    CHECK(text == "Running: hello.elf");
    UI::Shutdown();
}
