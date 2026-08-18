# ChuckStation 5 Architecture Reference

ChuckStation 5 components are organized in `source/chuckstation5/` with public headers in `include/ChuckStation5/`.

Subsystem dependencies flow down from `Runtime` / `Process` through `Execution`, `Cpu`, `Syscalls`, `Memory`, `GPU`, and `KernelRuntime`.
