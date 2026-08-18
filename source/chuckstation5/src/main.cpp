// ChuckStation5 – application entry point
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
// Built on top of Kyty (MIT, Copyright © 2021 InoriRus)
// ChuckStation5 is a research-oriented PlayStation 5 emulator framework.
// It does NOT and WILL NOT bundle, download, or embed firmware,
// cryptographic keys, or copyrighted game content.  Users must
// supply their own legally-obtained firmware.
#include "ChuckStation5/Audio/Audio.h"
#include "ChuckStation5/Config/Config.h"
#include "ChuckStation5/Debugger/Debugger.h"
#include "ChuckStation5/Filesystem/Filesystem.h"
#include "ChuckStation5/GPU/GPU.h"
#include "ChuckStation5/Input/Input.h"
#include "ChuckStation5/Kernel/Kernel.h"
#include "ChuckStation5/Loader/Loader.h"
#include "ChuckStation5/Logger/Logger.h"
#include "ChuckStation5/Renderer/RendererBackend.h"
#include "ChuckStation5/UI/UI.h"

// Production modules (conditionally compiled)
#if defined(CHUCKSTATION5_ENABLE_CRASH_HANDLER) && CHUCKSTATION5_ENABLE_CRASH_HANDLER
#include "ChuckStation5/CrashHandler/CrashHandler.h"
#endif
#if defined(CHUCKSTATION5_ENABLE_METRICS) && CHUCKSTATION5_ENABLE_METRICS
#include "ChuckStation5/Metrics/Metrics.h"
#endif
#if defined(CHUCKSTATION5_ENABLE_SAVESTATES) && CHUCKSTATION5_ENABLE_SAVESTATES
#include "ChuckStation5/SaveState/SaveState.h"
#endif
#if defined(CHUCKSTATION5_ENABLE_INPUT_MAPPING) && CHUCKSTATION5_ENABLE_INPUT_MAPPING
#include "ChuckStation5/InputMapping/InputMapping.h"
#endif

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

void PrintBanner()
{
    CHUCKSTATION5_INFO("╔══════════════════════════════════════════════════════════╗");
    CHUCKSTATION5_INFO("║  ChuckStation5 v1.0.0  –  Production PS5 Emulator Framework      ║");
    CHUCKSTATION5_INFO("║  Repository: github.com/libaerto/chuckstation5-windows            ║");
    CHUCKSTATION5_INFO("║  Based on Kyty (MIT © 2021 InoriRus)                    ║");
    CHUCKSTATION5_INFO("║  ChuckStation5 additions: MIT © 2024-2026 libaerto Contributors   ║");
    CHUCKSTATION5_INFO("╚══════════════════════════════════════════════════════════╝");
    CHUCKSTATION5_INFO("NOTICE: Provide your own legally-obtained PS5 firmware.");
    CHUCKSTATION5_INFO("ChuckStation5 does not supply, bundle, or download firmware.");
}

bool LoadOrCreateConfig(const std::filesystem::path& cfgPath)
{
    ChuckStation5::Config::Reset();
    if (std::filesystem::exists(cfgPath))
        return ChuckStation5::Config::Load(cfgPath);

    CHUCKSTATION5_WARN("No config found at %s – writing defaults.", cfgPath.string().c_str());
    return ChuckStation5::Config::Save(cfgPath);
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // ── 1. Logger ──────────────────────────────────────────────────────────
    ChuckStation5::Logger::Init("chuckstation5.log", /*console=*/true, ChuckStation5::Logger::Level::Info);
    PrintBanner();

    // ── 2. Crash Handler (early – before anything else) ───────────────────
#if defined(CHUCKSTATION5_ENABLE_CRASH_HANDLER) && CHUCKSTATION5_ENABLE_CRASH_HANDLER
    ChuckStation5::CrashHandler::Install("crashdumps");
    CHUCKSTATION5_INFO("[Main] Crash handler installed (minidumps → crashdumps/).");
#endif

    // ── 3. Metrics ────────────────────────────────────────────────────────
#if defined(CHUCKSTATION5_ENABLE_METRICS) && CHUCKSTATION5_ENABLE_METRICS
    ChuckStation5::Metrics::Init();
    CHUCKSTATION5_INFO("[Main] Metrics system initialised.");
#endif

    // ── 4. Config ─────────────────────────────────────────────────────────
    std::filesystem::path cfgPath = "chuckstation5.toml";
    if (argc >= 2) cfgPath = argv[1];

    if (!LoadOrCreateConfig(cfgPath))
        CHUCKSTATION5_WARN("Config load issues – continuing with defaults.");

    const auto& cfg = ChuckStation5::Config::Get();

    // ── 5. Firmware check ─────────────────────────────────────────────────
    if (!ChuckStation5::Config::ValidateFirmwarePath(cfg.emulator.firmwarePath))
    {
        CHUCKSTATION5_WARN("Firmware not configured.  "
                  "Set emulator.firmwarePath in %s.", cfgPath.string().c_str());
        // Non-fatal: continue so UI can guide the user.
    }

    // ── 6. Kernel / Memory ────────────────────────────────────────────────
    ChuckStation5::Kernel::Init();

    // ── 7. Loader ─────────────────────────────────────────────────────────
    ChuckStation5::Loader::Init();

    // ── 8. Filesystem VFS ─────────────────────────────────────────────────
    ChuckStation5::Filesystem::Init();
    if (!cfg.emulator.gameContentPath.empty())
        ChuckStation5::Filesystem::Mount(ChuckStation5::Filesystem::MountPoint::App0,
                                cfg.emulator.gameContentPath);
    if (!cfg.emulator.saveDataPath.empty())
        ChuckStation5::Filesystem::Mount(ChuckStation5::Filesystem::MountPoint::SaveData,
                                cfg.emulator.saveDataPath);
    if (!cfg.emulator.firmwarePath.empty())
        ChuckStation5::Filesystem::Mount(ChuckStation5::Filesystem::MountPoint::System,
                                cfg.emulator.firmwarePath);

    // ── 9. Input ──────────────────────────────────────────────────────────
    ChuckStation5::Input::Init();

    // ── 10. Input Mapping ─────────────────────────────────────────────────
#if defined(CHUCKSTATION5_ENABLE_INPUT_MAPPING) && CHUCKSTATION5_ENABLE_INPUT_MAPPING
    ChuckStation5::InputMapping::Init();
    ChuckStation5::InputMapping::LoadProfile("Default");
    CHUCKSTATION5_INFO("[Main] Input mapping initialised with Default profile.");
#endif

    // ── 11. Audio ─────────────────────────────────────────────────────────
    {
        ChuckStation5::Audio::AudioConfig acfg;
        acfg.sampleRate    = cfg.audio.sampleRate;
        acfg.bufferSamples = cfg.audio.bufferSize;
        acfg.masterVolume  = cfg.audio.masterVolume;
        ChuckStation5::Audio::Init(acfg);
    }

    // ── 12. Debugger ──────────────────────────────────────────────────────
    if (cfg.debug.enableDebugger)
        ChuckStation5::Debugger::Init();

    // ── 13. Renderer backend ──────────────────────────────────────────────
    auto renderer = ChuckStation5::Renderer::CreateBackend(cfg.graphics.backend);
    if (renderer)
    {
        ChuckStation5::Renderer::SwapChainDesc sc;
        sc.width  = cfg.graphics.width;
        sc.height = cfg.graphics.height;
        sc.vsync  = cfg.graphics.vsync;
        renderer->Init(cfg.graphics, sc);
        ChuckStation5::UI::SetBackendName(std::string(renderer->Name()));
    }

    // ── 14. GPU command translator ────────────────────────────────────────
    if (renderer)
        ChuckStation5::GPU::Init(renderer.get());

    // ── 15. Save State system ─────────────────────────────────────────────
#if defined(CHUCKSTATION5_ENABLE_SAVESTATES) && CHUCKSTATION5_ENABLE_SAVESTATES
    ChuckStation5::SaveState::Init("savestates");
    CHUCKSTATION5_INFO("[Main] Save state system initialised (directory: savestates/).");
#endif

    // ── 16. UI ────────────────────────────────────────────────────────────
    ChuckStation5::UI::Init();

    // Bridge Logger → UI console pane
    ChuckStation5::Logger::AddSink([](ChuckStation5::Logger::Level lvl, std::string_view tag, std::string_view msg) {
        ChuckStation5::UI::LogLine line;
        line.level   = static_cast<uint8_t>(lvl);
        line.tag     = std::string(tag);
        line.message = std::string(msg);
        ChuckStation5::UI::PushLogLine(line);
    });

    // Handle UI events
    ChuckStation5::UI::RegisterEventCallback([&](ChuckStation5::UI::UIEvent ev, const std::string& payload) {
        switch (ev)
        {
            case ChuckStation5::UI::UIEvent::LoadElf:
                { ChuckStation5::Loader::ExecutableInfo _info; ChuckStation5::Loader::LoadExecutable(payload, _info); }
                break;
            case ChuckStation5::UI::UIEvent::SetFirmwarePath:
                ChuckStation5::Config::GetMutable().emulator.firmwarePath = payload;
                ChuckStation5::Config::ValidateFirmwarePath(payload);
                break;
            case ChuckStation5::UI::UIEvent::StartEmulation:
                ChuckStation5::Loader::Execute();
                ChuckStation5::UI::SetEmulationActive(true);
                break;
            case ChuckStation5::UI::UIEvent::StopEmulation:
                ChuckStation5::Loader::Reset();
                ChuckStation5::UI::SetEmulationActive(false);
                break;
            case ChuckStation5::UI::UIEvent::Quit:
                ChuckStation5::UI::RequestExit();
                break;
            default:
                break;
        }
    });

    CHUCKSTATION5_INFO("[Main] All subsystems initialised. Entering main loop.");
    ChuckStation5::UI::Run();

    // ── Teardown (reverse init order) ─────────────────────────────────────
    CHUCKSTATION5_INFO("[Main] Shutting down subsystems...");
    ChuckStation5::UI::Shutdown();
    ChuckStation5::GPU::Shutdown();
    if (renderer) renderer->Shutdown();
    if (cfg.debug.enableDebugger) ChuckStation5::Debugger::Shutdown();
#if defined(CHUCKSTATION5_ENABLE_SAVESTATES) && CHUCKSTATION5_ENABLE_SAVESTATES
    ChuckStation5::SaveState::Shutdown();
#endif
    ChuckStation5::Audio::Shutdown();
#if defined(CHUCKSTATION5_ENABLE_INPUT_MAPPING) && CHUCKSTATION5_ENABLE_INPUT_MAPPING
    ChuckStation5::InputMapping::Shutdown();
#endif
    ChuckStation5::Input::Shutdown();
    ChuckStation5::Loader::Shutdown();
    ChuckStation5::Filesystem::Shutdown();
    ChuckStation5::Kernel::Shutdown();
#if defined(CHUCKSTATION5_ENABLE_METRICS) && CHUCKSTATION5_ENABLE_METRICS
    ChuckStation5::Metrics::LogAll();
    ChuckStation5::Metrics::Shutdown();
#endif
    ChuckStation5::Logger::Shutdown();
#if defined(CHUCKSTATION5_ENABLE_CRASH_HANDLER) && CHUCKSTATION5_ENABLE_CRASH_HANDLER
    ChuckStation5::CrashHandler::Uninstall();
#endif

    return EXIT_SUCCESS;
}
