// PS5x – Renderer backend abstraction
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
#pragma once

#include "PS5x/Config/Config.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace PS5x::Renderer
{

// ── Forward declarations ──────────────────────────────────────────────────

struct SwapChainDesc
{
	void* nativeWindow = nullptr;       ///< HWND on Windows
	void* nativeWindowHandle = nullptr; ///< alias for nativeWindow (HWND)
	uint32_t width = 1920;
	uint32_t height = 1080;
	uint32_t bufferCount = 2;
	bool vsync = true;
};

struct FrameStats
{
	uint64_t frameIndex = 0;
	uint64_t framesPresented = 0;
	double frameDeltaMs = 0.0;
	double gpuTimeMs = 0.0;
	double cpuTimeMs = 0.0;
};

// ── IRendererBackend interface ────────────────────────────────────────────

/// Pure-virtual interface every graphics backend must implement.
/// Concrete backends: VulkanBackend, DX11Backend, DX12Backend, OpenGLBackend.
class IRendererBackend
{
  public:
	virtual ~IRendererBackend() = default;

	// ── Lifecycle ─────────────────────────────────────────────────────────
	virtual bool Init(const Config::GraphicsConfig& cfg, const SwapChainDesc& sc) = 0;
	virtual void Shutdown() = 0;

	// ── Per-frame ─────────────────────────────────────────────────────────
	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void Present() = 0;

	// ── Resize ────────────────────────────────────────────────────────────
	virtual bool Resize(uint32_t width, uint32_t height) = 0;

	// ── Introspection ─────────────────────────────────────────────────────
	virtual std::string_view Name() const = 0;
	virtual Config::GraphicsBackend BackendType() const = 0;
	virtual const FrameStats& Stats() const = 0;
};

// ── Factory ──────────────────────────────────────────────────────────────

/// Create a backend instance for the requested API.
/// Returns nullptr if the backend is unavailable on this system.
std::unique_ptr<IRendererBackend> CreateBackend(Config::GraphicsBackend type);

/// Returns a bitmask of available backends on the current system.
uint32_t AvailableBackends();

} // namespace PS5x::Renderer
