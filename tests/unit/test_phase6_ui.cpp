// ChuckStation5 – Phase 6 UI tests (panels, workspace, status feeds)
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ChuckStation5/UI/UI.h"
#include <filesystem>

using namespace ChuckStation5::UI;

// ── PanelName ─────────────────────────────────────────────────────────────

TEST_CASE("Phase6::UI::PanelName::AllPanels", "[ui][phase6]")
{
    CHECK(std::string(PanelName(Panel::ExecutionDashboard)) == "Execution Dashboard");
    CHECK(std::string(PanelName(Panel::RuntimeTimeline))    == "Runtime Timeline");
    CHECK(std::string(PanelName(Panel::MemoryInspector))    == "Memory Inspector");
    CHECK(std::string(PanelName(Panel::GpuStatistics))      == "GPU Statistics");
    CHECK(std::string(PanelName(Panel::EventViewer))        == "Event Viewer");
    CHECK(std::string(PanelName(Panel::ControllerMonitor))  == "Controller Monitor");
    CHECK(std::string(PanelName(Panel::FilesystemBrowser))  == "Filesystem Browser");
    CHECK(std::string(PanelName(Panel::PerformanceGraphs))  == "Performance Graphs");
    CHECK(std::string(PanelName(Panel::ThreadInspector))    == "Thread Inspector");
    CHECK(std::string(PanelName(Panel::ModuleViewer))       == "Module Viewer");
    CHECK(std::string(PanelName(Panel::ShaderCacheViewer))  == "Shader Cache Viewer");
}

// ── Show / Hide panel ─────────────────────────────────────────────────────

TEST_CASE("Phase6::UI::Panel::DefaultVisible", "[ui][phase6]")
{
    // Panels default to visible
    CHECK(IsPanelVisible(Panel::ExecutionDashboard));
    CHECK(IsPanelVisible(Panel::MemoryInspector));
}

TEST_CASE("Phase6::UI::Panel::ShowHide", "[ui][phase6]")
{
    ShowPanel(Panel::EventViewer, true);
    CHECK(IsPanelVisible(Panel::EventViewer));
    HidePanel(Panel::EventViewer);
    CHECK_FALSE(IsPanelVisible(Panel::EventViewer));
    ShowPanel(Panel::EventViewer);
    CHECK(IsPanelVisible(Panel::EventViewer));
}

TEST_CASE("Phase6::UI::Panel::ShowWithFalse", "[ui][phase6]")
{
    ShowPanel(Panel::PerformanceGraphs, false);
    CHECK_FALSE(IsPanelVisible(Panel::PerformanceGraphs));
    ShowPanel(Panel::PerformanceGraphs, true);
}

TEST_CASE("Phase6::UI::Panel::IndependentVisibility", "[ui][phase6]")
{
    ShowPanel(Panel::GpuStatistics,    true);
    HidePanel(Panel::ControllerMonitor);
    CHECK(IsPanelVisible(Panel::GpuStatistics));
    CHECK_FALSE(IsPanelVisible(Panel::ControllerMonitor));
    ShowPanel(Panel::ControllerMonitor);
}

// ── Panel state ────────────────────────────────────────────────────────────

TEST_CASE("Phase6::UI::PanelState::GetDefault", "[ui][phase6]")
{
    auto s = GetPanelState(Panel::ThreadInspector);
    CHECK(s.panel  == Panel::ThreadInspector);
    CHECK(s.width  > 0.f);
    CHECK(s.height > 0.f);
}

TEST_CASE("Phase6::UI::PanelState::SetAndGet", "[ui][phase6]")
{
    PanelState st;
    st.panel   = Panel::ModuleViewer;
    st.visible = true;
    st.docked  = true;
    st.x       = 100.f;
    st.y       = 200.f;
    st.width   = 640.f;
    st.height  = 480.f;
    SetPanelState(st);

    auto got = GetPanelState(Panel::ModuleViewer);
    CHECK(got.docked == true);
    CHECK(got.x      == Catch::Approx(100.f));
    CHECK(got.y      == Catch::Approx(200.f));
    CHECK(got.width  == Catch::Approx(640.f));
    CHECK(got.height == Catch::Approx(480.f));
}

TEST_CASE("Phase6::UI::PanelState::PanelIdPreserved", "[ui][phase6]")
{
    auto s = GetPanelState(Panel::ShaderCacheViewer);
    CHECK(s.panel == Panel::ShaderCacheViewer);
}

// ── Status feeds ──────────────────────────────────────────────────────────

TEST_CASE("Phase6::UI::Feed::UpdateExecStatus", "[ui][phase6]")
{
    ExecutionStatus s;
    s.state      = "Running";
    s.frameIndex = 1234;
    s.syscalls   = 500;
    REQUIRE_NOTHROW(UpdateExecutionStatus(s));
}

TEST_CASE("Phase6::UI::Feed::UpdateGpuStatus", "[ui][phase6]")
{
    GpuStatus s;
    s.submits      = 10;
    s.flips        = 5;
    s.activeQueues = 3;
    s.gpuFrameMs   = 6.5;
    REQUIRE_NOTHROW(UpdateGpuStatus(s));
}

TEST_CASE("Phase6::UI::Feed::UpdateMemStatus", "[ui][phase6]")
{
    MemoryStatus s;
    s.allocatedBytes = 256 * 1024 * 1024ULL;
    s.peakBytes      = 300 * 1024 * 1024ULL;
    s.allocCount     = 10000;
    s.freeCount      = 9800;
    REQUIRE_NOTHROW(UpdateMemoryStatus(s));
}

TEST_CASE("Phase6::UI::Feed::MultipleUpdatesNoRace", "[ui][phase6]")
{
    // Rapid sequential updates should not crash
    for (int i = 0; i < 50; ++i) {
        ExecutionStatus s;
        s.state      = "Running";
        s.frameIndex = static_cast<uint64_t>(i);
        UpdateExecutionStatus(s);
    }
    SUCCEED("50 rapid UpdateExecutionStatus calls without crash");
}

// ── Workspace save / load ─────────────────────────────────────────────────

TEST_CASE("Phase6::UI::Workspace::SaveAndLoad", "[ui][phase6]")
{
    std::string path = "/tmp/chuckstation5_test_workspace.txt";

    // Set a known state
    PanelState s;
    s.panel   = Panel::MemoryInspector;
    s.visible = false;
    s.x       = 42.f;
    s.y       = 84.f;
    s.width   = 320.f;
    s.height  = 240.f;
    SetPanelState(s);

    CHECK(SaveWorkspace(path));
    CHECK(std::filesystem::exists(path));

    // Reset to defaults, then reload
    PanelState reset;
    reset.panel   = Panel::MemoryInspector;
    reset.visible = true;
    reset.x       = 0.f;
    SetPanelState(reset);

    CHECK(LoadWorkspace(path));

    auto loaded = GetPanelState(Panel::MemoryInspector);
    CHECK(loaded.visible == false);
    CHECK(loaded.x       == Catch::Approx(42.f));
    CHECK(loaded.width   == Catch::Approx(320.f));

    std::filesystem::remove(path);
}

TEST_CASE("Phase6::UI::Workspace::SaveToInvalidPath", "[ui][phase6]")
{
    CHECK_FALSE(SaveWorkspace("/no_such_dir/workspace.txt"));
}

TEST_CASE("Phase6::UI::Workspace::LoadMissingFile", "[ui][phase6]")
{
    CHECK_FALSE(LoadWorkspace("/tmp/chuckstation5_nonexistent_workspace_xyz.txt"));
}
