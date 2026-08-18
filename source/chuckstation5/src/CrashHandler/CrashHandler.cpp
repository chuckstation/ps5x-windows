// ChuckStation5 – Crash Handler implementation
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
#include "ChuckStation5/CrashHandler/CrashHandler.h"

#include "ChuckStation5/Logger/Logger.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

// ── Platform headers ────────────────────────────────────────────────────────
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#   include <windows.h>
#   include <dbghelp.h>
// clang-format on
#pragma comment(lib, "dbghelp.lib")
#else
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#endif

namespace ChuckStation5::CrashHandler
{

// ── Internal state ──────────────────────────────────────────────────────────
namespace
{

struct State
{
	std::atomic<bool> installed{false};
	std::string dumpDir;
	CrashCallback userCallback{nullptr};
	std::mutex mtx;

#if defined(_WIN32)
	LPTOP_LEVEL_EXCEPTION_FILTER prevFilter{nullptr};
#else
	struct sigaction oldSegv
	{
	};
	struct sigaction oldAbrt
	{
	};
	struct sigaction oldFpe
	{
	};
#endif

	static State& Get()
	{
		static State s;
		return s;
	}
};

// ── Helpers ─────────────────────────────────────────────────────────────────

std::string GenerateDumpFilename()
{
	using namespace std::chrono;
	const auto now = system_clock::now();
	const auto time = system_clock::to_time_t(now);
	char buf[128];
	std::strftime(buf, sizeof(buf), "crash_%Y%m%d_%H%M%S", std::localtime(&time));
	return std::string(buf) + ".dmp";
}

CrashInfo BuildCrashInfo(uint32_t code, uint64_t faultAddr, uint64_t rip, uint64_t rsp, const std::string& module,
						 const std::string& message)
{
	CrashInfo ci;
	ci.exceptionCode = code;
	ci.faultAddr = faultAddr;
	ci.rip = rip;
	ci.rsp = rsp;
	ci.module = module;
	ci.message = message;
	return ci;
}

void InvokeCallbackAndTerminate(const CrashInfo& info)
{
	CHUCKSTATION5_FATAL("CRASH: code=0x%08X fault=0x%016llX rip=0x%016llX rsp=0x%016llX module=%s msg=%s", info.exceptionCode,
			   static_cast<unsigned long long>(info.faultAddr), static_cast<unsigned long long>(info.rip),
			   static_cast<unsigned long long>(info.rsp), info.module.empty() ? "<unknown>" : info.module.c_str(),
			   info.message.empty() ? "<none>" : info.message.c_str());

	auto& st = State::Get();
	if (st.userCallback)
		st.userCallback(info);

	// Attempt to write a minidump to the configured directory
	if (!st.dumpDir.empty())
	{
		std::string path = st.dumpDir + "/" + GenerateDumpFilename();
		WriteMinidump(path);
	}

	std::_Exit(1);
}

// ── Windows SEH handler ────────────────────────────────────────────────────
#if defined(_WIN32)

std::string GetModuleForAddress(uint64_t addr)
{
	HMODULE hMod = nullptr;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCSTR>(addr), &hMod))
	{
		char name[MAX_PATH] = {};
		GetModuleFileNameA(hMod, name, MAX_PATH);
		return name;
	}
	return "<unknown>";
}

LONG WINAPI SehExceptionHandler(EXCEPTION_POINTERS* ep)
{
	auto& st = State::Get();

	const auto* rec = ep->ExceptionRecord;
	const auto* ctx = ep->ContextRecord;

	uint32_t code = static_cast<uint32_t>(rec->ExceptionCode);
	uint64_t faultAddr = rec->ExceptionInformation[1];
	uint64_t rip = ctx->Rip;
	uint64_t rsp = ctx->Rsp;
	std::string mod = GetModuleForAddress(rip);

	char msgBuf[256] = {};
	std::snprintf(msgBuf, sizeof(msgBuf), "SEH exception 0x%08X at 0x%016llX", code,
				  static_cast<unsigned long long>(rip));

	CrashInfo info = BuildCrashInfo(code, faultAddr, rip, rsp, mod, msgBuf);

	// Write minidump before invoking callback
	if (!st.dumpDir.empty())
	{
		std::string path = st.dumpDir + "\\" + GenerateDumpFilename();
		HANDLE hFile =
			CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			MINIDUMP_EXCEPTION_INFORMATION mei;
			mei.ThreadId = GetCurrentThreadId();
			mei.ExceptionPointers = ep;
			mei.ClientPointers = FALSE;

			MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpWithDataSegs, &mei, nullptr,
							  nullptr);
			CloseHandle(hFile);

			CHUCKSTATION5_FATAL("Minidump written to %s", path.c_str());
		}
	}

	InvokeCallbackAndTerminate(info);

	// Never reached, but satisfy the signature
	return EXCEPTION_EXECUTE_HANDLER;
}

#endif // _WIN32

// ── Linux signal handler ───────────────────────────────────────────────────
#if !defined(_WIN32)

void LinuxSignalHandler(int sig, siginfo_t* si, void* /*ctx*/)
{
	auto& st = State::Get();

	uint32_t code = static_cast<uint32_t>(sig);
	uint64_t faultAddr = reinterpret_cast<uint64_t>(si->si_addr);

	// Capture backtrace
	void* btBuf[64];
	int btLen = backtrace(btBuf, 64);

	uint64_t rip = 0;
	uint64_t rsp = 0;
	if (btLen > 0)
		rip = reinterpret_cast<uint64_t>(btBuf[0]);

	char msgBuf[512] = {};
	std::snprintf(msgBuf, sizeof(msgBuf), "Signal %d (si_code=%d) at 0x%016llX", sig, si->si_code,
				  static_cast<unsigned long long>(faultAddr));

	CrashInfo info = BuildCrashInfo(code, faultAddr, rip, rsp, "<unknown>", msgBuf);

	// Log backtrace via CHUCKSTATION5_FATAL
	char** btSymbols = backtrace_symbols(btBuf, btLen);
	if (btSymbols)
	{
		CHUCKSTATION5_FATAL("Backtrace (%d frames):", btLen);
		for (int i = 0; i < btLen; ++i)
			CHUCKSTATION5_FATAL("  [%02d] %s", i, btSymbols[i]);
		free(btSymbols);
	}

	// Write text-based core dump info
	if (!st.dumpDir.empty())
	{
		std::string path = st.dumpDir + "/" + GenerateDumpFilename();
		WriteMinidump(path);
	}

	InvokeCallbackAndTerminate(info);
}

#endif // !_WIN32

} // anonymous namespace

// ── Public API ──────────────────────────────────────────────────────────────

bool Install(const std::string& dumpDir)
{
	auto& st = State::Get();
	std::lock_guard lock(st.mtx);

	if (st.installed.load(std::memory_order_acquire))
	{
		CHUCKSTATION5_WARN("CrashHandler already installed – skipping");
		return true;
	}

	st.dumpDir = dumpDir;

#if defined(_WIN32)
	st.prevFilter = SetUnhandledExceptionFilter(SehExceptionHandler);
	CHUCKSTATION5_INFO("CrashHandler installed (Windows SEH, prevFilter=%p, dumpDir=%s)", st.prevFilter,
			  dumpDir.empty() ? "<none>" : dumpDir.c_str());
#else
	struct sigaction sa
	{
	};
	sa.sa_sigaction = LinuxSignalHandler;
	sa.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGSEGV, &sa, &st.oldSegv);
	sigaction(SIGABRT, &sa, &st.oldAbrt);
	sigaction(SIGFPE, &sa, &st.oldFpe);

	CHUCKSTATION5_INFO("CrashHandler installed (Linux signals: SIGSEGV/SIGABRT/SIGFPE, dumpDir=%s)",
			  dumpDir.empty() ? "<none>" : dumpDir.c_str());
#endif

	st.installed.store(true, std::memory_order_release);
	return true;
}

void Uninstall()
{
	auto& st = State::Get();
	std::lock_guard lock(st.mtx);

	if (!st.installed.load(std::memory_order_acquire))
		return;

#if defined(_WIN32)
	SetUnhandledExceptionFilter(st.prevFilter);
	CHUCKSTATION5_INFO("CrashHandler uninstalled (Windows SEH restored)");
#else
	sigaction(SIGSEGV, &st.oldSegv, nullptr);
	sigaction(SIGABRT, &st.oldAbrt, nullptr);
	sigaction(SIGFPE, &st.oldFpe, nullptr);
	CHUCKSTATION5_INFO("CrashHandler uninstalled (Linux signals restored)");
#endif

	st.installed.store(false, std::memory_order_release);
	st.dumpDir.clear();
	st.userCallback = nullptr;
}

void SetCallback(CrashCallback cb)
{
	auto& st = State::Get();
	std::lock_guard lock(st.mtx);
	st.userCallback = cb;
}

bool WriteMinidump(const std::string& path)
{
	if (path.empty())
	{
		CHUCKSTATION5_ERROR("WriteMinidump: empty path");
		return false;
	}

	CHUCKSTATION5_INFO("Writing minidump to %s", path.c_str());

#if defined(_WIN32)
	HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		CHUCKSTATION5_ERROR("WriteMinidump: CreateFile failed (err=%lu)", GetLastError());
		return false;
	}

	BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpWithDataSegs, nullptr,
								nullptr, nullptr);
	CloseHandle(hFile);

	if (!ok)
	{
		CHUCKSTATION5_ERROR("WriteMinidump: MiniDumpWriteDump failed (err=%lu)", GetLastError());
		return false;
	}

	CHUCKSTATION5_INFO("Minidump written successfully: %s", path.c_str());
	return true;
#else
	// On Linux, write a text-based crash dump with backtrace
	FILE* fp = std::fopen(path.c_str(), "w");
	if (!fp)
	{
		CHUCKSTATION5_ERROR("WriteMinidump: fopen failed for %s", path.c_str());
		return false;
	}

	std::fprintf(fp, "=== ChuckStation5 Crash Dump ===\n");

	// Timestamp
	using namespace std::chrono;
	const auto now = system_clock::now();
	const auto time = system_clock::to_time_t(now);
	char tsBuf[64];
	std::strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
	std::fprintf(fp, "Timestamp: %s\n", tsBuf);

	std::fprintf(fp, "PID: %d\n", getpid());

	// Backtrace
	void* btBuf[64];
	int btLen = backtrace(btBuf, 64);
	char** btSymbols = backtrace_symbols(btBuf, btLen);
	if (btSymbols)
	{
		std::fprintf(fp, "Backtrace (%d frames):\n", btLen);
		for (int i = 0; i < btLen; ++i)
			std::fprintf(fp, "  [%02d] %s\n", i, btSymbols[i]);
		free(btSymbols);
	}

	std::fclose(fp);
	CHUCKSTATION5_INFO("Crash dump written successfully: %s", path.c_str());
	return true;
#endif
}

void ReportCrash(const std::string& message)
{
	CrashInfo info = BuildCrashInfo(0, 0, 0, 0, "", message);
	InvokeCallbackAndTerminate(info);
}

} // namespace ChuckStation5::CrashHandler
