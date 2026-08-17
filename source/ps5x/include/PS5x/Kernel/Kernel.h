// PS5x – Kernel module
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
#pragma once

#include <cstddef>
#include <cstdint>

namespace PS5x::Kernel
{

// ── Virtual memory ────────────────────────────────────────────────────────

enum class MapFlags : uint32_t
{
	Read = 1 << 0,
	Write = 1 << 1,
	Execute = 1 << 2,
	Fixed = 1 << 3,
};

void* VirtualAlloc(void* hint, size_t size, MapFlags flags);
bool VirtualFree(void* addr, size_t size);
bool VirtualProtect(void* addr, size_t size, MapFlags flags);

// ── PS5 memory regions ────────────────────────────────────────────────────
//
// PS5 uses a fixed virtual address layout (like PS4 before it):
//   0x00000001_00000000 – 0x000000FF_FFFFFFFF  application
//   0x00000100_00000000 – 0x0000FFFF_FFFFFFFF  kernel / flex-heap
//
static constexpr uint64_t APP_BASE_ADDR = 0x00000001'00000000ULL;
static constexpr uint64_t APP_MAX_ADDR = 0x000000FF'FFFFFFFFULL;

// ── Flex-heap ─────────────────────────────────────────────────────────────

void* FlexHeapAlloc(size_t size, size_t align = 16);
void FlexHeapFree(void* ptr);

// ── Lifecycle ─────────────────────────────────────────────────────────────

void Init();
void Shutdown();

} // namespace PS5x::Kernel
