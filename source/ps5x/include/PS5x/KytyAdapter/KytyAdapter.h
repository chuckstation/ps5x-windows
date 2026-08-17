// PS5x – KytyAdapter
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// The KytyAdapter is the ONLY place in PS5x that may include Kyty headers.
// All other PS5x code must use PS5x interfaces exclusively.
//
// Current state (Phase 2): adapter stubs forward to PS5x implementations.
// When Kyty is available (Windows + PS5X_INCLUDE_KYTY=ON), real Kyty calls
// will be inserted here behind the same interface.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace PS5x::KytyAdapter
{

// ── Lifecycle ─────────────────────────────────────────────────────────────
/// Initialize the Kyty subsystems needed by PS5x.
/// On non-Windows builds or when Kyty is excluded this is a no-op.
bool Init();
void Shutdown();
bool IsAvailable(); ///< True if the Kyty runtime is live.

// ── Runtime linker bridge ─────────────────────────────────────────────────
/// Load a PS5 ELF through Kyty's RuntimeLinker.
/// Returns 0 on success, negative on error.
int LoadProgram(const std::filesystem::path& path);
int ExecuteProgram();
void UnloadProgram();

// ── Kernel bridge ─────────────────────────────────────────────────────────
/// Allocate virtual memory via Kyty's memory subsystem.
void* VirtualAlloc(void* hint, uint64_t size, int prot);
bool VirtualFree(void* addr);

// ── Graphics bridge ───────────────────────────────────────────────────────
/// Initialise Kyty's Vulkan layer (delegates to Kyty's GraphicsRender).
bool InitVulkan(void* nativeWindowHandle, uint32_t width, uint32_t height);
void ShutdownVulkan();
void BeginFrame();
void EndFrame();
void Present();

// ── Audio bridge ──────────────────────────────────────────────────────────
bool InitAudio(uint32_t sampleRate, uint16_t channels, uint16_t bufferFrames);
void ShutdownAudio();

// ── Log bridge ────────────────────────────────────────────────────────────
/// Route Kyty's internal log output to PS5x Logger.
void InstallLogBridge();

} // namespace PS5x::KytyAdapter
