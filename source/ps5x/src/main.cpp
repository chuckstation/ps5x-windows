// PS5x – application entry point
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
//
// PS5x is a research-oriented PlayStation 5 emulator framework.
// It does NOT and WILL NOT bundle, download, or embed firmware,
// cryptographic keys, or copyrighted game content.  Users must
// supply their own legally-obtained firmware.
#include "PS5x/Audio/Audio.h"
#include "PS5x/Config/Config.h"
#include "PS5x/Debugger/Debugger.h"
#include "PS5x/Filesystem/Filesystem.h"
#include "PS5x/GPU/GPU.h"
#include "PS5x/Input/Input.h"
#include "PS5x/Kernel/Kernel.h"
#include "PS5x/Loader/Loader.h"
#include "PS5x/Logger/Logger.h"
#include "PS5x/Renderer/RendererBackend.h"
#include "PS5x/UI/UI.h"

// Production modules (conditionally compiled)
#if defined(PS5X_ENABLE_CRASH_HANDLER) && PS5X_ENABLE_CRASH_HANDLER
#include "PS5x/CrashHandler/CrashHandler.h"
#endif
#if defined(PS5X_ENABLE_METRICS) && PS5X_ENABLE_METRICS
#include "PS5x/Metrics/Metrics.h"
#endif
#if defined(PS5X_ENABLE_SAVESTATES) && PS5X_ENABLE_SAVESTATES
#include "PS5x/SaveState/SaveState.h"
#endif
#if defined(PS5X_ENABLE_INPUT_MAPPING) && PS5X_ENABLE_INPUT_MAPPING
#include "PS5x/InputMapping/InputMapping.h"
#endif

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

void PrintBanner()
{
    PS5X_INFO("╔══════════════════════════════════════════════════════════╗");
    PS5X_INFO("║  PS5x v1.0.0  –  Production PS5 Emulator Framework      ║");
    PS5X_INFO("║  Repository: github.com/libaerto/ps5x-windows            ║");
    PS5X_INFO("║  Based on Kyty (MIT © 2021 InoriRus)                    ║");
    PS5X_INFO("║  PS5x additions: MIT © 2024-2026 libaerto Contributors   ║");
    PS5X_INFO("╚══════════════════════════════════════════════════════════╝");
    PS5X_INFO("NOTICE: Provide your own legally-obtained PS5 firmware.");
    PS5X_INFO("PS5x does not supply, bundle, or download firmware.");
}

bool LoadOrCreateConfig(const std::filesystem::path& cfgPath)
{
    PS5x::Config::Reset();
    if (std::filesystem::exists(cfgPath))
        return PS5x::Config::Load(cfgPath);

    PS5X_WARN("No config found at %s – writing defaults.", cfgPath.string().c_str());
    return PS5x::Config::Save(cfgPath);
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // ── 1. Logger ──────────────────────────────────────────────────────────
    PS5x::Logger::Init("ps5x.log", /*console=*/true, PS5x::Logger::Level::Info);
    PrintBanner();

    // ── 2. Crash Handler (early – before anything else) ───────────────────
#if defined(PS5X_ENABLE_CRASH_HANDLER) && PS5X_ENABLE_CRASH_HANDLER
    PS5x::CrashHandler::Install("crashdumps");
    PS5X_INFO("[Main] Crash handler installed (minidumps → crashdumps/).");
#endif

    // ── 3. Metrics ────────────────────────────────────────────────────────
#if defined(PS5X_ENABLE_METRICS) && PS5X_ENABLE_METRICS
    PS5x::Metrics::Init();
    PS5X_INFO("[Main] Metrics system initialised.");
#endif

    // ── 4. Config ─────────────────────────────────────────────────────────
    std::filesystem::path cfgPath = "ps5x.toml";
    if (argc >= 2) cfgPath = argv[1];

    if (!LoadOrCreateConfig(cfgPath))
        PS5X_WARN("Config load issues – continuing with defaults.");

    const auto& cfg = PS5x::Config::Get();

    // ── 5. Firmware check ─────────────────────────────────────────────────
    if (!PS5x::Config::ValidateFirmwarePath(cfg.emulator.firmwarePath))
    {
        PS5X_WARN("Firmware not configured.  "
                  "Set emulator.firmwarePath in %s.", cfgPath.string().c_str());
        // Non-fatal: continue so UI can guide the user.
    }

    // ── 6. Kernel / Memory ────────────────────────────────────────────────
    PS5x::Kernel::Init();

    // ── 7. Loader ─────────────────────────────────────────────────────────
    PS5x::Loader::Init();

    // ── 8. Filesystem VFS ─────────────────────────────────────────────────
    PS5x::Filesystem::Init();
    if (!cfg.emulator.gameContentPath.empty())
        PS5x::Filesystem::Mount(PS5x::Filesystem::MountPoint::App0,
                                cfg.emulator.gameContentPath);
    if (!cfg.emulator.saveDataPath.empty())
        PS5x::Filesystem::Mount(PS5x::Filesystem::MountPoint::SaveData,
                                cfg.emulator.saveDataPath);
    if (!cfg.emulator.firmwarePath.empty())
        PS5x::Filesystem::Mount(PS5x::Filesystem::MountPoint::System,
                                cfg.emulator.firmwarePath);

    // ── 9. Input ──────────────────────────────────────────────────────────
    PS5x::Input::Init();

    // ── 10. Input Mapping ─────────────────────────────────────────────────
#if defined(PS5X_ENABLE_INPUT_MAPPING) && PS5X_ENABLE_INPUT_MAPPING
    PS5x::InputMapping::Init();
    PS5x::InputMapping::LoadProfile("Default");
    PS5X_INFO("[Main] Input mapping initialised with Default profile.");
#endif

    // ── 11. Audio ─────────────────────────────────────────────────────────
    {
        PS5x::Audio::AudioConfig acfg;
        acfg.sampleRate    = cfg.audio.sampleRate;
        acfg.bufferSamples = cfg.audio.bufferSize;
        acfg.masterVolume  = cfg.audio.masterVolume;
        PS5x::Audio::Init(acfg);
    }

    // ── 12. Debugger ──────────────────────────────────────────────────────
    if (cfg.debug.enableDebugger)
        PS5x::Debugger::Init();

    // ── 13. Renderer backend ──────────────────────────────────────────────
    auto renderer = PS5x::Renderer::CreateBackend(cfg.graphics.backend);
    if (renderer)
    {
        PS5x::Renderer::SwapChainDesc sc;
        sc.width  = cfg.graphics.width;
        sc.height = cfg.graphics.height;
        sc.vsync  = cfg.graphics.vsync;
        renderer->Init(cfg.graphics, sc);
        PS5x::UI::SetBackendName(std::string(renderer->Name()));
    }

    // ── 14. GPU command translator ────────────────────────────────────────
    if (renderer)
        PS5x::GPU::Init(renderer.get());

    // ── 15. Save State system ─────────────────────────────────────────────
#if defined(PS5X_ENABLE_SAVESTATES) && PS5X_ENABLE_SAVESTATES
    PS5x::SaveState::Init("savestates");
    PS5X_INFO("[Main] Save state system initialised (directory: savestates/).");
#endif

    // ── 16. UI ────────────────────────────────────────────────────────────
    PS5x::UI::Init(cfg.graphics.width, cfg.graphics.height, "PS5x v1.0.0");

    // Bridge Logger → UI console pane
    PS5x::Logger::AddSink([](PS5x::Logger::Level lvl, std::string_view tag, std::string_view msg) {
        PS5x::UI::LogLine line;
        line.level   = static_cast<uint8_t>(lvl);
        line.tag     = std::string(tag);
        line.message = std::string(msg);
        PS5x::UI::PushLogLine(line);
    });

    // Handle UI events
    PS5x::UI::RegisterEventCallback([&](PS5x::UI::UIEvent ev, const std::string& payload) {
        switch (ev)
        {
            case PS5x::UI::UIEvent::LoadElf:
                { PS5x::Loader::ExecutableInfo _info; PS5x::Loader::LoadExecutable(payload, _info); }
                break;
            case PS5x::UI::UIEvent::SetFirmwarePath:
                PS5x::Config::GetMutable().emulator.firmwarePath = payload;
                PS5x::Config::ValidateFirmwarePath(payload);
                break;
            case PS5x::UI::UIEvent::StartEmulation:
                PS5x::Loader::Execute();
                PS5x::UI::SetEmulationActive(true);
                break;
            case PS5x::UI::UIEvent::StopEmulation:
                PS5x::Loader::Reset();
                PS5x::UI::SetEmulationActive(false);
                break;
            case PS5x::UI::UIEvent::Quit:
                PS5x::UI::RequestExit();
                break;
            default:
                break;
        }
    });

    PS5X_INFO("[Main] All subsystems initialised. Entering main loop.");
    PS5x::UI::Run();

    // ── Teardown (reverse init order) ─────────────────────────────────────
    PS5X_INFO("[Main] Shutting down subsystems...");
    PS5x::UI::Shutdown();
    PS5x::GPU::Shutdown();
    if (renderer) renderer->Shutdown();
    if (cfg.debug.enableDebugger) PS5x::Debugger::Shutdown();
#if defined(PS5X_ENABLE_SAVESTATES) && PS5X_ENABLE_SAVESTATES
    PS5x::SaveState::Shutdown();
#endif
    PS5x::Audio::Shutdown();
#if defined(PS5X_ENABLE_INPUT_MAPPING) && PS5X_ENABLE_INPUT_MAPPING
    PS5x::InputMapping::Shutdown();
#endif
    PS5x::Input::Shutdown();
    PS5x::Loader::Shutdown();
    PS5x::Filesystem::Shutdown();
    PS5x::Kernel::Shutdown();
#if defined(PS5X_ENABLE_METRICS) && PS5X_ENABLE_METRICS
    PS5x::Metrics::LogAll();
    PS5x::Metrics::Shutdown();
#endif
    PS5x::Logger::Shutdown();
#if defined(PS5X_ENABLE_CRASH_HANDLER) && PS5X_ENABLE_CRASH_HANDLER
    PS5x::CrashHandler::Uninstall();
#endif

    return EXIT_SUCCESS;
}
