// ChuckStation5 – GPU unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/GPU/GPU.h"
#include "ChuckStation5/Renderer/RendererBackend.h"
#include "ChuckStation5/Config/Config.h"

TEST_CASE("GPU – Init requires valid backend", "[gpu]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);

    // Null backend should fail
    REQUIRE(!ChuckStation5::GPU::Init(nullptr));

    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("GPU – Init / Shutdown with stub Vulkan backend", "[gpu]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Config::Reset();

    auto backend = ChuckStation5::Renderer::CreateBackend(ChuckStation5::Config::GraphicsBackend::Vulkan);
    REQUIRE(backend != nullptr);

    ChuckStation5::Renderer::SwapChainDesc sc;
    sc.width = 1280; sc.height = 720;
    REQUIRE(backend->Init(ChuckStation5::Config::Get().graphics, sc));

    REQUIRE(ChuckStation5::GPU::Init(backend.get()));
    ChuckStation5::GPU::Shutdown();
    backend->Shutdown();

    ChuckStation5::Logger::Shutdown();
}

TEST_CASE("GPU – Submit empty command buffers", "[gpu]")
{
    ChuckStation5::Logger::Init("", false, ChuckStation5::Logger::Level::Off);
    ChuckStation5::Config::Reset();

    auto backend = ChuckStation5::Renderer::CreateBackend(ChuckStation5::Config::GraphicsBackend::OpenGL);
    REQUIRE(backend);
    ChuckStation5::Renderer::SwapChainDesc sc;
    sc.width = 640; sc.height = 480;
    backend->Init(ChuckStation5::Config::Get().graphics, sc);
    ChuckStation5::GPU::Init(backend.get());

    ChuckStation5::GPU::CommandBuffer dcb, ccb;
    REQUIRE(ChuckStation5::GPU::Submit(dcb, ccb));

    ChuckStation5::GPU::ClearRenderTarget(0, 0.f, 0.f, 0.f, 1.f);
    ChuckStation5::GPU::ClearDepthTarget(1.0f, 0);

    ChuckStation5::GPU::Shutdown();
    backend->Shutdown();
    ChuckStation5::Logger::Shutdown();
}
