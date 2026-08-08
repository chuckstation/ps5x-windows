// PS5x – GPU Command Processor
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
//
// Processes GPU command lists submitted via GPU::Submit().
// Translates opaque command packets into renderer backend calls.
//
// Phase 7 supports a simple command encoding used by the PS5x homebrew
// validation suite. Full PM4 packet support is a future milestone.
//
// Command list format (Phase 7):
//   Each command is a variable-length packet:
//     [4 bytes] opcode (CommandOpcode enum)
//     [N bytes] payload (opcode-dependent)
#pragma once

#include "PS5x/GPU/GPU.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace PS5x::Renderer { class IRendererBackend; }

namespace PS5x::CommandProcessor {

// ── Command opcodes ───────────────────────────────────────────────────────
enum class CommandOpcode : uint32_t
{
    Nop              = 0x0000,
    SetRenderTarget  = 0x0001,
    SetDepthTarget   = 0x0002,
    ClearColor       = 0x0010,
    ClearDepth       = 0x0011,
    DrawIndexed      = 0x0100,
    DrawDirect       = 0x0101,
    Dispatch         = 0x0200,  ///< compute
    SetViewport      = 0x0300,
    SetScissor       = 0x0301,
    BarrierTransition= 0x0400,
    WaitFence        = 0x0500,
    SignalFence      = 0x0501,
    BeginRenderPass  = 0x0600,
    EndRenderPass    = 0x0601,
    PushDebugLabel   = 0x0700,
    PopDebugLabel    = 0x0701,
    End              = 0xFFFF,  ///< terminates a command list
};
const char* OpcodeName(CommandOpcode op);

// ── Viewport / scissor ────────────────────────────────────────────────────
struct Viewport
{
    float x = 0.f, y = 0.f;
    float width = 1920.f, height = 1080.f;
    float minDepth = 0.f, maxDepth = 1.f;
};

struct Scissor
{
    int32_t  x = 0, y = 0;
    uint32_t width = 1920, height = 1080;
};

// ── Command builder ───────────────────────────────────────────────────────
/// Helper for constructing command lists in the PS5x format.
class CommandList
{
public:
    void Nop();
    void SetRenderTarget(uint32_t slot, const GPU::RenderTarget& rt);
    void SetDepthTarget(const GPU::DepthTarget& dt);
    void ClearColor(uint32_t slot, float r, float g, float b, float a);
    void ClearDepth(float depth, uint8_t stencil = 0);
    void DrawDirect(uint32_t vertexCount, uint32_t instanceCount = 1,
                    uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0);
    void Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ);
    void SetViewport(const Viewport& vp);
    void SetScissor(const Scissor& sc);
    void Barrier(const GPU::Barrier& b);
    void WaitFence(GPU::FenceHandle h);
    void SignalFence(GPU::FenceHandle h);
    void BeginRenderPass(const std::string& label = "");
    void EndRenderPass();
    void PushDebugLabel(const std::string& label);
    void PopDebugLabel();
    void End();

    /// Return the raw bytes of the command list.
    const std::vector<uint8_t>& Data() const { return _buf; }
    size_t Size() const { return _buf.size(); }
    void Clear() { _buf.clear(); }

private:
    std::vector<uint8_t> _buf;

    void WriteU32(uint32_t v);
    void WriteU64(uint64_t v);
    void WriteF32(float v);
    void WriteI32(int32_t v);
    void WriteBytes(const void* data, size_t n);
    void WriteOpcode(CommandOpcode op);
};

// ── Statistics ────────────────────────────────────────────────────────────
struct ProcessorStats
{
    uint64_t  commandListsProcessed = 0;
    uint64_t  commandsProcessed     = 0;
    uint64_t  drawCalls             = 0;
    uint64_t  computeDispatches     = 0;
    uint64_t  barriers              = 0;
    uint64_t  renderPassBegins      = 0;
    uint64_t  unknownCommands       = 0;
    double    totalProcessMs        = 0.0;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
bool Init(Renderer::IRendererBackend* backend);
void Shutdown();

// ── Processing ────────────────────────────────────────────────────────────

/// Process a raw command list buffer.
/// @param data  Pointer to the command list bytes.
/// @param size  Byte count.
/// @returns number of commands processed, or -1 on error.
int32_t Process(const uint8_t* data, size_t size);

/// Process a CommandList object.
int32_t Process(const CommandList& list);

/// Process a GPU::CommandBuffer (wraps the raw-bytes path).
int32_t Process(const GPU::CommandBuffer& cb);

// ── Statistics ────────────────────────────────────────────────────────────
ProcessorStats GetStats();
void           ResetStats();

} // namespace PS5x::CommandProcessor
