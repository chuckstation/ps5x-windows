// ChuckStation5 – Crash Handler
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#pragma once
#include <cstdint>
#include <string>

namespace ChuckStation5::CrashHandler {

struct CrashInfo {
    uint32_t    exceptionCode = 0;
    uint64_t    faultAddr     = 0;
    uint64_t    rip           = 0;
    uint64_t    rsp           = 0;
    std::string module;
    std::string message;
};

using CrashCallback = void(*)(const CrashInfo& info);

// Install platform-specific crash handler (SEH on Windows, signal on Linux)
bool Install(const std::string& dumpDir = "");
void Uninstall();

// Register user callback invoked on crash before termination
void SetCallback(CrashCallback cb);

// Write a minidump (Windows) or core dump info (Linux)
bool WriteMinidump(const std::string& path);

// Manual crash report (for assertion failures etc.)
void ReportCrash(const std::string& message);

} // namespace ChuckStation5::CrashHandler
