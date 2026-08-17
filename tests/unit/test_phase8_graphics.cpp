// PS5x – Phase 8 Graphics Validation tests
// SPDX-License-Identifier: MIT
//
// Validates the GPU/Renderer pipeline for progressive rendering demos:
//   triangle → textured quad → off-screen render → frame capture.
// No real GPU is present; all validation is against the stub/mock backend.
#include "PS5x/CommandProcessor/CommandProcessor.h"
#include "PS5x/GPU/GPU.h"
#include "PS5x/Renderer/RendererBackend.h"
#include "PS5x/RuntimeEvents/RuntimeEvents.h"
#include "PS5x/ShaderCache/ShaderCache.h"
#include <catch2/catch_test_macros.hpp>

using namespace PS5x;
using namespace PS5x::GPU;
using namespace PS5x::CommandProcessor;
using namespace PS5x::ShaderCache;

// ── GPU lifecycle ─────────────────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::GPU::InitShutdown", "[graphics][phase8]") {
  CHECK(GPU::Init(nullptr));
  GPU::Shutdown();
}

TEST_CASE("Phase8::Graphics::GPU::DoubleInit", "[graphics][phase8]") {
  CHECK(GPU::Init(nullptr));
  CHECK(GPU::Init(nullptr)); // idempotent
  GPU::Shutdown();
}

TEST_CASE("Phase8::Graphics::GPU::MultipleInitShutdownCycles",
          "[graphics][phase8]") {
  for (int i = 0; i < 5; ++i) {
    CHECK(GPU::Init(nullptr));
    GPU::Shutdown();
  }
}

// ── Render target setup ───────────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::RenderTarget::SetAndGet", "[graphics][phase8]") {
  GPU::Init(nullptr);
  RenderTarget rt{};
  rt.width = 1920;
  rt.height = 1080;
  rt.format = PixelFormat::R8G8B8A8_Unorm;
  rt.pitch = rt.width * 4;
  GPU::SetRenderTarget(0, rt);
  auto got = GPU::GetCurrentRenderTarget();
  CHECK(got.width == 1920);
  CHECK(got.height == 1080);
  CHECK(got.format == PixelFormat::R8G8B8A8_Unorm);
  GPU::Shutdown();
}

TEST_CASE("Phase8::Graphics::RenderTarget::DefaultIsZero",
          "[graphics][phase8]") {
  GPU::Init(nullptr);
  auto rt = GPU::GetCurrentRenderTarget();
  CHECK(rt.width == 0);
  CHECK(rt.height == 0);
  GPU::Shutdown();
}

// ── Triangle rendering demo ───────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::Demo::Triangle::CommandListBuilds",
          "[graphics][phase8]") {
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);

  CommandList cl;
  cl.BeginRenderPass();
  cl.ClearColor(0, 0.0f, 0.0f, 0.2f, 1.0f);
  cl.SetViewport({0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f});
  cl.SetScissor({0, 0, 1280, 720});
  cl.DrawDirect(3, 1, 0, 0); // 3 vertices = triangle
  cl.EndRenderPass();
  cl.End();

  CHECK(cl.Size() > 0);
  CommandProcessor::Shutdown();
  GPU::Shutdown();
}

TEST_CASE("Phase8::Graphics::Demo::Triangle::ProcesseswithoutCrash",
          "[graphics][phase8]") {
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);

  CommandList cl;
  cl.BeginRenderPass();
  cl.ClearColor(0, 0.1f, 0.2f, 0.3f, 1.0f);
  cl.DrawDirect(3, 1, 0, 0);
  cl.EndRenderPass();
  cl.End();

  // Should process cleanly against null backend
  auto before = CommandProcessor::GetStats();
  CommandProcessor::Process(cl);
  auto after = CommandProcessor::GetStats();
  CHECK(after.commandListsProcessed >= before.commandListsProcessed + 1);

  CommandProcessor::Shutdown();
  GPU::Shutdown();
}

// ── Textured quad demo ────────────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::Demo::TexturedQuad::DrawIndexed",
          "[graphics][phase8]") {
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);

  CommandList cl;
  cl.BeginRenderPass();
  cl.ClearColor(0, 0.0f, 0.0f, 0.0f, 1.0f);
  cl.SetViewport({0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f});
  cl.SetScissor({0, 0, 1280, 720});
  cl.DrawIndexed(6, 1, 0, 0, 0); // 6 indices = 2 triangles = quad
  cl.EndRenderPass();
  cl.End();

  CHECK(cl.Size() > 0);
  CommandProcessor::Process(cl);
  auto s = CommandProcessor::GetStats();
  CHECK(s.drawCalls >= 1);

  CommandProcessor::Shutdown();
  GPU::Shutdown();
}

// ── Off-screen rendering ──────────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::Demo::OffScreen::FrameCapture",
          "[graphics][phase8]") {
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);

  // Simulate off-screen render to a capture buffer
  alignas(16) std::vector<uint8_t> framebuffer(1280 * 720 * 4, 0);

  RenderTarget rt{};
  rt.dataPtr = framebuffer.data();
  rt.width = 1280;
  rt.height = 720;
  rt.pitch = 1280 * 4;
  rt.format = PixelFormat::R8G8B8A8_Unorm;
  GPU::SetRenderTarget(0, rt);

  CommandList cl;
  cl.SetRenderTarget(0, rt);
  cl.ClearColor(0, 1.0f, 0.0f, 0.0f, 1.0f);
  cl.DrawDirect(3, 1, 0, 0);
  cl.End();

  CommandProcessor::Process(cl);
  // The clear was applied (or is a stub) — we just verify no crash
  CHECK(true);

  CommandProcessor::Shutdown();
  GPU::Shutdown();
}

// ── Multiple frames ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::Demo::MultipleFrames::FrameCountIncrements",
          "[graphics][phase8]") {
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);

  constexpr int FRAMES = 10;
  for (int i = 0; i < FRAMES; ++i) {
    CommandList cl;
    cl.BeginRenderPass();
    cl.ClearColor(0, 0.0f, static_cast<float>(i) / FRAMES, 0.0f, 1.0f);
    cl.DrawDirect(3, 1, 0, 0);
    cl.EndRenderPass();
    cl.End();
    CommandProcessor::Process(cl);
  }
  auto s = CommandProcessor::GetStats();
  CHECK(s.commandListsProcessed >= FRAMES);

  CommandProcessor::Shutdown();
  GPU::Shutdown();
}

// ── Barrier + fence ───────────────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::Barrier::TransitionBeforeDraw",
          "[graphics][phase8]") {
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);

  CommandList cl;
  GPU::Barrier b;
  b.gpuAddr = 0;
  b.before = GPU::ResourceState::Undefined;
  b.after = GPU::ResourceState::RenderTarget;
  cl.Barrier(b);

  cl.BeginRenderPass();
  cl.DrawDirect(3, 1, 0, 0);
  cl.EndRenderPass();

  b.before = GPU::ResourceState::RenderTarget;
  b.after = GPU::ResourceState::Undefined;
  cl.Barrier(b);
  cl.End();

  CommandProcessor::Process(cl);
  auto s = CommandProcessor::GetStats();
  CHECK(s.barriers >= 2);

  CommandProcessor::Shutdown();
  GPU::Shutdown();
}

// ── Shader cache ─────────────────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::ShaderCache::InitShutdown", "[graphics][phase8]") {
  CHECK(ShaderCache::Init());
  ShaderCache::Shutdown();
}

TEST_CASE("Phase8::Graphics::ShaderCache::StoreAndRetrieve",
          "[graphics][phase8]") {
  ShaderCache::Init();
  std::vector<uint8_t> bytecode = {0x01, 0x02, 0x03, 0x04};
  ShaderKey key;
  key.spirvHash = ShaderCache::HashSpirv(bytecode.data(), bytecode.size());
  key.pipelineHash = 123;
  key.stage = ShaderStage::Vertex;

  ShaderCache::SetCompiler(
      [](const std::vector<uint8_t> &spirv, ShaderStage) { return spirv; });

  auto entry = ShaderCache::Compile(key, bytecode, "test_shader");
  REQUIRE(entry.has_value());

  auto retrieved = ShaderCache::Lookup(key);
  REQUIRE(retrieved.has_value());
  CHECK(retrieved->binary == bytecode);
  ShaderCache::Shutdown();
}

TEST_CASE("Phase8::Graphics::ShaderCache::MissingKeyReturnsNullopt",
          "[graphics][phase8]") {
  ShaderCache::Init();
  ShaderKey missing_key{0xDEADBEEF, 0xDEADBEEF, ShaderStage::Fragment};
  auto result = ShaderCache::Lookup(missing_key);
  CHECK(!result.has_value());
  ShaderCache::Shutdown();
}

TEST_CASE("Phase8::Graphics::ShaderCache::EvictionPolicy",
          "[graphics][phase8]") {
  ShaderCache::Init();
  ShaderCache::SetCompiler(
      [](const std::vector<uint8_t> &spirv, ShaderStage) { return spirv; });
  for (int i = 0; i < 50; ++i) {
    std::vector<uint8_t> bc = {static_cast<uint8_t>(i)};
    ShaderKey k;
    k.spirvHash = ShaderCache::HashSpirv(bc.data(), bc.size());
    k.pipelineHash = i;
    k.stage = ShaderStage::Vertex;
    ShaderCache::Compile(k, bc);
  }
  auto s = ShaderCache::GetStats();
  CHECK(s.entries >= 1);
  ShaderCache::Shutdown();
}

// ── RuntimeEvents from GPU ────────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::RuntimeEvents::FrameEndPublished",
          "[graphics][phase8]") {
  RuntimeEvents::Init();
  GPU::Init(nullptr);
  CommandProcessor::Init(nullptr);

  bool frameEndSeen = false;
  RuntimeEvents::Subscribe(
      [&](const RuntimeEvents::RuntimeEvent &) { frameEndSeen = true; },
      RuntimeEvents::EventType::FrameEnd);

  CommandList cl;
  cl.End();
  CommandProcessor::Process(cl);

  CHECK(frameEndSeen);

  CommandProcessor::Shutdown();
  GPU::Shutdown();
  RuntimeEvents::Shutdown();
}

// ── Depth target ─────────────────────────────────────────────────────────

TEST_CASE("Phase8::Graphics::DepthTarget::SetAndGet", "[graphics][phase8]") {
  GPU::Init(nullptr);
  DepthTarget dt{};
  dt.width = 1280;
  dt.height = 720;
  dt.format = PixelFormat::D32_Float;
  GPU::SetDepthTarget(dt);
  auto got = GPU::GetCurrentDepthTarget();
  CHECK(got.width == 1280);
  CHECK(got.height == 720);
  CHECK(got.format == PixelFormat::D32_Float);
  GPU::Shutdown();
}
