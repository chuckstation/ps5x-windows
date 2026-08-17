// PS5x – GPU module
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// The GPU module sits between the guest command buffer submission (GNM/GNMX)
// and the renderer backend (Vulkan/DX11/DX12/GL).  In milestone 6 this will
// translate PS5 PM4 packets into backend draw calls.
#pragma once

#include <cstdint>
#include <memory>

namespace PS5x::Renderer
{
class IRendererBackend;
}

namespace PS5x::GPU
{

// ── Pixel formats (subset of PS5 DataFormat) ─────────────────────────────
enum class PixelFormat : uint32_t
{
	Unknown = 0,
	R8G8B8A8_Unorm = 1,
	B8G8R8A8_Unorm = 2,
	R16G16B16A16_Float = 3,
	R32G32B32A32_Float = 4,
	BC1_Unorm = 10,
	BC3_Unorm = 11,
	BC7_Unorm = 12,
	D32_Float = 20,
	D24_Unorm_S8 = 21,
};

// ── Render target ─────────────────────────────────────────────────────────
struct RenderTarget
{
	void* dataPtr = nullptr; ///< GPU-mapped address
	uint64_t gpuAddr = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t pitch = 0;
	PixelFormat format = PixelFormat::Unknown;
};

// ── Depth/stencil target ──────────────────────────────────────────────────
struct DepthTarget
{
	void* dataPtr = nullptr;
	uint64_t gpuAddr = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	PixelFormat format = PixelFormat::D32_Float;
};

// ── Command buffer ────────────────────────────────────────────────────────
/// Opaque handle to a guest command buffer (PM4 packet stream).
struct CommandBuffer
{
	const void* data = nullptr;
	uint64_t size = 0;
	uint64_t gpuAddr = 0;
};

// ── GPU flip / present ────────────────────────────────────────────────────
struct FlipArgs
{
	uint32_t videoOutHandle = 0;
	int32_t bufferIndex = 0;
	int64_t flipArg = 0;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(PS5x::Renderer::IRendererBackend* backend);
void Shutdown();

// ── Command submission ────────────────────────────────────────────────────
/// Submit a draw command buffer (GNM/GNMX PM4 stream).
bool Submit(const CommandBuffer& dcb, const CommandBuffer& ccb);

/// Flip the display (sceVideoOutSubmitFlip equivalent).
bool Flip(const FlipArgs& args);

// ── Resource management ───────────────────────────────────────────────────
bool SetRenderTarget(uint32_t slot, const RenderTarget& rt);
bool SetDepthTarget(const DepthTarget& dt);
DepthTarget GetCurrentDepthTarget();
RenderTarget GetCurrentRenderTarget();
bool ClearRenderTarget(uint32_t slot, float r, float g, float b, float a);
bool ClearDepthTarget(float depth, uint8_t stencil);

// ── Phase 6: Command queue abstraction ───────────────────────────────────

using CmdQueueHandle = int32_t;
static constexpr CmdQueueHandle INVALID_CMD_QUEUE = -1;

enum class QueueType : uint8_t
{
	Graphics = 0,
	Compute = 1,
	Transfer = 2
};

CmdQueueHandle CreateQueue(QueueType type = QueueType::Graphics);
bool DestroyQueue(CmdQueueHandle h);
bool SubmitToQueue(CmdQueueHandle h, const CommandBuffer& dcb, const CommandBuffer& ccb);

// ── Phase 6: GPU synchronisation ─────────────────────────────────────────

using FenceHandle = int32_t;
static constexpr FenceHandle INVALID_FENCE = -1;

FenceHandle CreateFence();
bool SignalFence(FenceHandle h);
bool WaitFence(FenceHandle h, uint64_t timeoutUs = UINT64_MAX);
bool IsFenceSignalled(FenceHandle h);
void DestroyFence(FenceHandle h);

// ── Phase 6: Resource barriers ────────────────────────────────────────────

enum class ResourceState : uint8_t
{
	Undefined = 0,
	RenderTarget = 1,
	ShaderRead = 2,
	TransferSrc = 3,
	TransferDst = 4,
	Present = 5,
	DepthWrite = 6,
};

struct Barrier
{
	uint64_t gpuAddr = 0;
	ResourceState before = ResourceState::Undefined;
	ResourceState after = ResourceState::Undefined;
};

bool InsertBarrier(const Barrier& barrier);
bool InsertBarriers(const Barrier* barriers, uint32_t count);

// ── Phase 6: GPU statistics ───────────────────────────────────────────────

struct GpuStats
{
	uint64_t submits = 0;
	uint64_t flips = 0;
	uint64_t barriers = 0;
	uint64_t fencesSignaled = 0;
	uint32_t activeQueues = 0;
};
GpuStats GetGpuStats();

} // namespace PS5x::GPU
