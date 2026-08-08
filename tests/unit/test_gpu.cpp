// PS5x – GPU unit tests
// SPDX-License-Identifier: MIT
#include <catch2/catch_test_macros.hpp>
#include "PS5x/Logger/Logger.h"
#include "PS5x/GPU/GPU.h"
#include "PS5x/Renderer/RendererBackend.h"
#include "PS5x/Config/Config.h"

TEST_CASE("GPU – Init requires valid backend", "[gpu]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);

    // Null backend should fail
    REQUIRE(!PS5x::GPU::Init(nullptr));

    PS5x::Logger::Shutdown();
}

TEST_CASE("GPU – Init / Shutdown with stub Vulkan backend", "[gpu]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Config::Reset();

    auto backend = PS5x::Renderer::CreateBackend(PS5x::Config::GraphicsBackend::Vulkan);
    REQUIRE(backend != nullptr);

    PS5x::Renderer::SwapChainDesc sc;
    sc.width = 1280; sc.height = 720;
    REQUIRE(backend->Init(PS5x::Config::Get().graphics, sc));

    REQUIRE(PS5x::GPU::Init(backend.get()));
    PS5x::GPU::Shutdown();
    backend->Shutdown();

    PS5x::Logger::Shutdown();
}

TEST_CASE("GPU – Submit empty command buffers", "[gpu]")
{
    PS5x::Logger::Init("", false, PS5x::Logger::Level::Off);
    PS5x::Config::Reset();

    auto backend = PS5x::Renderer::CreateBackend(PS5x::Config::GraphicsBackend::OpenGL);
    REQUIRE(backend);
    PS5x::Renderer::SwapChainDesc sc;
    sc.width = 640; sc.height = 480;
    backend->Init(PS5x::Config::Get().graphics, sc);
    PS5x::GPU::Init(backend.get());

    PS5x::GPU::CommandBuffer dcb, ccb;
    REQUIRE(PS5x::GPU::Submit(dcb, ccb));

    PS5x::GPU::ClearRenderTarget(0, 0.f, 0.f, 0.f, 1.f);
    PS5x::GPU::ClearDepthTarget(1.0f, 0);

    PS5x::GPU::Shutdown();
    backend->Shutdown();
    PS5x::Logger::Shutdown();
}
