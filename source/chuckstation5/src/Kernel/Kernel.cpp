// ChuckStation5 – Kernel implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "ChuckStation5/Kernel/Kernel.h"
#include "ChuckStation5/Logger/Logger.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

#include <cstdlib>

namespace ChuckStation5::Kernel {

namespace {

#ifdef _WIN32
DWORD ToWinProt(MapFlags flags)
{
    auto f = static_cast<uint32_t>(flags);
    bool r = f & static_cast<uint32_t>(MapFlags::Read);
    bool w = f & static_cast<uint32_t>(MapFlags::Write);
    bool x = f & static_cast<uint32_t>(MapFlags::Execute);
    if (x && w) return PAGE_EXECUTE_READWRITE;
    if (x && r) return PAGE_EXECUTE_READ;
    if (x)      return PAGE_EXECUTE;
    if (w)      return PAGE_READWRITE;
    if (r)      return PAGE_READONLY;
    return PAGE_NOACCESS;
}
#endif

} // anonymous namespace

void Init()
{
    CHUCKSTATION5_INFO("[Kernel] Memory subsystem initialised.");
}

void Shutdown()
{
    CHUCKSTATION5_INFO("[Kernel] Memory subsystem shutdown.");
}

void* VirtualAlloc(void* hint, size_t size, MapFlags flags)
{
#ifdef _WIN32
    DWORD prot = ToWinProt(flags);
    void* ptr  = ::VirtualAlloc(hint, size, MEM_RESERVE | MEM_COMMIT, prot);
    if (!ptr)
        CHUCKSTATION5_ERROR("[Kernel] VirtualAlloc failed: size=%zu", size);
    return ptr;
#else
    int prot = PROT_NONE;
    auto f = static_cast<uint32_t>(flags);
    if (f & static_cast<uint32_t>(MapFlags::Read))    prot |= PROT_READ;
    if (f & static_cast<uint32_t>(MapFlags::Write))   prot |= PROT_WRITE;
    if (f & static_cast<uint32_t>(MapFlags::Execute)) prot |= PROT_EXEC;
    void* ptr = mmap(hint, size, prot, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED)
    {
        CHUCKSTATION5_ERROR("[Kernel] mmap failed: size=%zu", size);
        return nullptr;
    }
    return ptr;
#endif
}

bool VirtualFree(void* addr, size_t size)
{
#ifdef _WIN32
    (void)size;
    return ::VirtualFree(addr, 0, MEM_RELEASE) != 0;
#else
    return munmap(addr, size) == 0;
#endif
}

bool VirtualProtect(void* addr, size_t size, MapFlags flags)
{
#ifdef _WIN32
    DWORD old = 0;
    return ::VirtualProtect(addr, size, ToWinProt(flags), &old) != 0;
#else
    int prot = PROT_NONE;
    auto f = static_cast<uint32_t>(flags);
    if (f & static_cast<uint32_t>(MapFlags::Read))    prot |= PROT_READ;
    if (f & static_cast<uint32_t>(MapFlags::Write))   prot |= PROT_WRITE;
    if (f & static_cast<uint32_t>(MapFlags::Execute)) prot |= PROT_EXEC;
    return mprotect(addr, size, prot) == 0;
#endif
}

void* FlexHeapAlloc(size_t size, size_t align)
{
#ifdef _WIN32
    return _aligned_malloc(size, align);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, align, size) != 0)
        return nullptr;
    return ptr;
#endif
}

void FlexHeapFree(void* ptr)
{
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

} // namespace ChuckStation5::Kernel
