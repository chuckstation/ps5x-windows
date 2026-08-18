# ChuckStation 5 Architecture

ChuckStation 5 is structured as a modular set of independent subsystems written in C++20.

## Subsystems

- `Cpu`: x86-64 interpreter pipeline and instruction stepping.
- `Syscalls`: FreeBSD/Linux guest syscall translation layer.
- `Memory`: Host-backed guest virtual memory allocator.
- `KernelRuntime`: Object model for guest threads, synchronization primitives, and TLS.
- `GPU` / `CommandProcessor`: Command list recording and execution model for Vulkan/DX12/DX11 backends.
- `Audio`: Ring-buffer audio submission pipeline.
- `Input`: Controller and keyboard mapping.
- `Filesystem`: Virtual filesystem mount points (`/app0`, `/system`, `/savedata`).
