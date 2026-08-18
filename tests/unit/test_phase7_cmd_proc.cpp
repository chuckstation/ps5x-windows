// PS5x – Phase 7 CommandProcessor tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/CommandProcessor/CommandProcessor.h"
#include "PS5x/GPU/GPU.h"

using namespace PS5x::CommandProcessor;

// ── Lifecycle ──────────────────────────────────────────────────────────────

TEST_CASE("Phase7::CmdProc::Lifecycle::Init", "[cmd_proc][phase7]")
{
    CHECK(Init(nullptr));  // null backend — still initialises
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Lifecycle::DoubleInit", "[cmd_proc][phase7]")
{
    CHECK(Init(nullptr));
    CHECK(Init(nullptr));
    Shutdown();
}

// ── OpcodeName ────────────────────────────────────────────────────────────

TEST_CASE("Phase7::CmdProc::OpName::AllOpcodes", "[cmd_proc][phase7]")
{
    CHECK(std::string(OpcodeName(CommandOpcode::Nop))              == "Nop");
    CHECK(std::string(OpcodeName(CommandOpcode::SetRenderTarget))  == "SetRenderTarget");
    CHECK(std::string(OpcodeName(CommandOpcode::SetDepthTarget))   == "SetDepthTarget");
    CHECK(std::string(OpcodeName(CommandOpcode::ClearColor))       == "ClearColor");
    CHECK(std::string(OpcodeName(CommandOpcode::ClearDepth))       == "ClearDepth");
    CHECK(std::string(OpcodeName(CommandOpcode::DrawIndexed))      == "DrawIndexed");
    CHECK(std::string(OpcodeName(CommandOpcode::DrawDirect))       == "DrawDirect");
    CHECK(std::string(OpcodeName(CommandOpcode::Dispatch))         == "Dispatch");
    CHECK(std::string(OpcodeName(CommandOpcode::SetViewport))      == "SetViewport");
    CHECK(std::string(OpcodeName(CommandOpcode::SetScissor))       == "SetScissor");
    CHECK(std::string(OpcodeName(CommandOpcode::BarrierTransition))== "Barrier");
    CHECK(std::string(OpcodeName(CommandOpcode::WaitFence))        == "WaitFence");
    CHECK(std::string(OpcodeName(CommandOpcode::SignalFence))      == "SignalFence");
    CHECK(std::string(OpcodeName(CommandOpcode::BeginRenderPass))  == "BeginRenderPass");
    CHECK(std::string(OpcodeName(CommandOpcode::EndRenderPass))    == "EndRenderPass");
    CHECK(std::string(OpcodeName(CommandOpcode::PushDebugLabel))   == "PushDebugLabel");
    CHECK(std::string(OpcodeName(CommandOpcode::PopDebugLabel))    == "PopDebugLabel");
    CHECK(std::string(OpcodeName(CommandOpcode::End))              == "End");
}

// ── CommandList builder ────────────────────────────────────────────────────

TEST_CASE("Phase7::CmdProc::Builder::EmptyList", "[cmd_proc][phase7]")
{
    CommandList cl;
    CHECK(cl.Size() == 0);
    CHECK(cl.Data().empty());
}

TEST_CASE("Phase7::CmdProc::Builder::NopGrows", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.Nop();
    CHECK(cl.Size() == 4);  // 4-byte opcode
}

TEST_CASE("Phase7::CmdProc::Builder::EndGrows", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.End();
    CHECK(cl.Size() == 4);
}

TEST_CASE("Phase7::CmdProc::Builder::ClearColorSize", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.ClearColor(0, 0.f, 0.f, 0.f, 1.f);
    // opcode(4) + slot(4) + r,g,b,a(4*4) = 24
    CHECK(cl.Size() == 24);
}

TEST_CASE("Phase7::CmdProc::Builder::DrawDirectSize", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.DrawDirect(3);
    // opcode(4) + 4*uint32 = 20
    CHECK(cl.Size() == 20);
}

TEST_CASE("Phase7::CmdProc::Builder::DrawIndexedSize", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.DrawIndexed(6);
    // opcode(4) + 5*4 = 24
    CHECK(cl.Size() == 24);
}

TEST_CASE("Phase7::CmdProc::Builder::DispatchSize", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.Dispatch(1, 1, 1);
    // opcode(4) + 3*4 = 16
    CHECK(cl.Size() == 16);
}

TEST_CASE("Phase7::CmdProc::Builder::ViewportSize", "[cmd_proc][phase7]")
{
    CommandList cl;
    Viewport vp;
    cl.SetViewport(vp);
    // opcode(4) + 6 floats(24) = 28
    CHECK(cl.Size() == 28);
}

TEST_CASE("Phase7::CmdProc::Builder::ScissorSize", "[cmd_proc][phase7]")
{
    CommandList cl;
    Scissor sc;
    cl.SetScissor(sc);
    // opcode(4) + 2*int32 + 2*uint32 = 4+16 = 20
    CHECK(cl.Size() == 20);
}

TEST_CASE("Phase7::CmdProc::Builder::BarrierSize", "[cmd_proc][phase7]")
{
    CommandList cl;
    PS5x::GPU::Barrier b{0x1000, PS5x::GPU::ResourceState::Undefined, PS5x::GPU::ResourceState::RenderTarget};
    cl.Barrier(b);
    // opcode(4) + gpuAddr(8) + before(4) + after(4) = 20
    CHECK(cl.Size() == 20);
}

TEST_CASE("Phase7::CmdProc::Builder::Clear", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.Nop();
    cl.End();
    cl.Clear();
    CHECK(cl.Size() == 0);
    CHECK(cl.Data().empty());
}

TEST_CASE("Phase7::CmdProc::Builder::MultipleCommands", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.Nop();
    cl.Nop();
    cl.ClearColor(0, 0.f, 0.f, 0.f, 1.f);
    cl.DrawDirect(3);
    cl.End();
    CHECK(cl.Size() > 0);
}

TEST_CASE("Phase7::CmdProc::Builder::BeginEndRenderPass", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.BeginRenderPass("main_pass");
    cl.EndRenderPass();
    CHECK(cl.Size() > 0);
}

TEST_CASE("Phase7::CmdProc::Builder::DebugLabels", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.PushDebugLabel("shadow");
    cl.PopDebugLabel();
    CHECK(cl.Size() > 0);
}

TEST_CASE("Phase7::CmdProc::Builder::FenceCommands", "[cmd_proc][phase7]")
{
    CommandList cl;
    cl.WaitFence(1);
    cl.SignalFence(2);
    // 2 * (opcode(4) + handle(4)) = 16
    CHECK(cl.Size() == 16);
}

// ── Processing ────────────────────────────────────────────────────────────

TEST_CASE("Phase7::CmdProc::Process::EmptyList", "[cmd_proc][phase7]")
{
    Init(nullptr);
    CommandList cl;
    cl.End();
    int32_t n = Process(cl);
    CHECK(n >= 0);
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Process::NopCounted", "[cmd_proc][phase7]")
{
    Init(nullptr);
    ResetStats();
    CommandList cl;
    cl.Nop();
    cl.Nop();
    cl.Nop();
    cl.End();
    int32_t n = Process(cl);
    CHECK(n >= 3);
    auto s = GetStats();
    CHECK(s.commandsProcessed >= 3);
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Process::DrawCallCounted", "[cmd_proc][phase7]")
{
    Init(nullptr);
    ResetStats();
    CommandList cl;
    cl.DrawDirect(3);
    cl.DrawIndexed(6);
    cl.End();
    Process(cl);
    CHECK(GetStats().drawCalls >= 2);
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Process::ComputeCounted", "[cmd_proc][phase7]")
{
    Init(nullptr);
    ResetStats();
    CommandList cl;
    cl.Dispatch(8, 8, 1);
    cl.End();
    Process(cl);
    CHECK(GetStats().computeDispatches >= 1);
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Process::BarrierCounted", "[cmd_proc][phase7]")
{
    Init(nullptr);
    ResetStats();
    CommandList cl;
    PS5x::GPU::Barrier b{0x2000, PS5x::GPU::ResourceState::Undefined, PS5x::GPU::ResourceState::RenderTarget};
    cl.Barrier(b);
    cl.End();
    Process(cl);
    CHECK(GetStats().barriers >= 1);
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Process::RenderPassCounted", "[cmd_proc][phase7]")
{
    Init(nullptr);
    ResetStats();
    CommandList cl;
    cl.BeginRenderPass("test");
    cl.EndRenderPass();
    cl.End();
    Process(cl);
    CHECK(GetStats().renderPassBegins >= 1);
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Process::CommandListsIncrement", "[cmd_proc][phase7]")
{
    Init(nullptr);
    ResetStats();
    CommandList cl;
    cl.Nop(); cl.End();
    Process(cl);
    Process(cl);
    CHECK(GetStats().commandListsProcessed >= 2);
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Process::NullDataSafe", "[cmd_proc][phase7]")
{
    Init(nullptr);
    CHECK(Process(nullptr, 0) == 0);
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Process::FullScene", "[cmd_proc][phase7]")
{
    Init(nullptr);
    ResetStats();
    CommandList cl;
    cl.BeginRenderPass("main");
    cl.ClearColor(0, 0.1f, 0.2f, 0.4f, 1.f);
    cl.ClearDepth(1.f);
    Viewport vp; cl.SetViewport(vp);
    Scissor  sc; cl.SetScissor(sc);
    cl.DrawDirect(3, 1);
    cl.DrawIndexed(6, 1);
    cl.EndRenderPass();
    cl.End();
    int32_t n = Process(cl);
    CHECK(n >= 7);
    auto s = GetStats();
    CHECK(s.drawCalls      >= 2);
    CHECK(s.renderPassBegins >= 1);
    Shutdown();
}

// ── Statistics ─────────────────────────────────────────────────────────────

TEST_CASE("Phase7::CmdProc::Stats::Reset", "[cmd_proc][phase7]")
{
    Init(nullptr);
    CommandList cl;
    cl.DrawDirect(3); cl.End();
    Process(cl);
    ResetStats();
    auto s = GetStats();
    CHECK(s.drawCalls == 0);
    CHECK(s.commandListsProcessed == 0);
    Shutdown();
}

TEST_CASE("Phase7::CmdProc::Stats::TimingPositive", "[cmd_proc][phase7]")
{
    Init(nullptr);
    ResetStats();
    CommandList cl;
    for (int i = 0; i < 10; ++i) cl.Nop();
    cl.End();
    Process(cl);
    CHECK(GetStats().totalProcessMs >= 0.0);
    Shutdown();
}
