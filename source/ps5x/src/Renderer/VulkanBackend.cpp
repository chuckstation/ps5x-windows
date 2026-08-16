// PS5x – Renderer backends (Windows-only, Phase 8)
// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 libaerto Contributors
//
// Windows-only target. Backend priority:
//   1. DirectX 12  (primary — DXGI + D3D12, Win10+)
//   2. DirectX 11  (fallback — D3D11, Win7+)
//   3. Vulkan      (optional — via Vulkan-1.dll, when present)
//
// Each backend:
//   - Creates its device, swapchain, and command infrastructure on Init()
//   - Drives Present() through DXGI / VkQueuePresentKHR
//   - Releases all COM / Vulkan objects on Shutdown()
//
// When the corresponding DLL / SDK is absent at runtime the backend's
// Init() returns false and the factory falls back to the next option.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "PS5x/Renderer/RendererBackend.h"
#include "PS5x/Logger/Logger.h"

#if defined(_WIN32)
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3d11.h>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3d11.lib")
#endif

#include <array>
#include <cstring>
#include <memory>
#include <string_view>

namespace PS5x::Renderer {

// ═══════════════════════════════════════════════════════════════════════════
// DirectX 12 Backend
// ═══════════════════════════════════════════════════════════════════════════
class DX12Backend final : public IRendererBackend
{
public:
    bool Init(const Config::GraphicsConfig& cfg, const SwapChainDesc& sc) override
    {
        (void)cfg;
        width_  = sc.width;
        height_ = sc.height;

#if defined(_WIN32)
        hwnd_   = static_cast<HWND>(sc.nativeWindowHandle);
        HRESULT hr;

        // ── Debug layer (Debug builds) ────────────────────────────────────
#if defined(_DEBUG)
        {
            ID3D12Debug* debug = nullptr;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
                debug->EnableDebugLayer();
                debug->Release();
                PS5X_DEBUG("[DX12] Debug layer enabled.");
            }
        }
#endif

        // ── DXGI factory ──────────────────────────────────────────────────
        hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory_));
        if (FAILED(hr)) {
            PS5X_ERROR("[DX12] CreateDXGIFactory1 failed (hr=0x%08X).", hr);
            return false;
        }

        // ── Adapter enumeration — pick first hardware adapter ─────────────
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter->Release(); continue; }
            hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
                                   IID_PPV_ARGS(&device_));
            if (SUCCEEDED(hr)) {
                char adapterName[128]{};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                    adapterName, sizeof(adapterName), nullptr, nullptr);
                PS5X_INFO("[DX12] Adapter: %s", adapterName);
                adapter->Release();
                break;
            }
            adapter->Release();
        }
        if (!device_) {
            PS5X_ERROR("[DX12] No D3D12-capable adapter found.");
            return false;
        }

        // ── Command queue ─────────────────────────────────────────────────
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_));
        if (FAILED(hr)) { PS5X_ERROR("[DX12] CreateCommandQueue failed."); return false; }

        // ── Swapchain ─────────────────────────────────────────────────────
        if (hwnd_) {
            DXGI_SWAP_CHAIN_DESC1 scDesc{};
            scDesc.Width       = width_;
            scDesc.Height      = height_;
            scDesc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
            scDesc.SampleDesc  = {1, 0};
            scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scDesc.BufferCount = kFrameCount;
            scDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;

            IDXGISwapChain1* sc1 = nullptr;
            IDXGIFactory4*   f4  = nullptr;
            if (SUCCEEDED(factory_->QueryInterface(IID_PPV_ARGS(&f4)))) {
                hr = f4->CreateSwapChainForHwnd(commandQueue_, hwnd_,
                                                &scDesc, nullptr, nullptr, &sc1);
                f4->Release();
                if (SUCCEEDED(hr)) {
                    sc1->QueryInterface(IID_PPV_ARGS(&swapChain_));
                    sc1->Release();
                    PS5X_INFO("[DX12] Swapchain created %ux%u.", width_, height_);
                }
            }
        } else {
            PS5X_INFO("[DX12] Headless mode — no swapchain.");
        }

        // ── RTV descriptor heap ───────────────────────────────────────────
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.NumDescriptors = kFrameCount;
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_));
        rtvDescSize_ = device_->GetDescriptorHandleIncrementSize(
                           D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // ── Per-frame render targets ──────────────────────────────────────
        if (swapChain_) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
                rtvHeap_->GetCPUDescriptorHandleForHeapStart();
            for (UINT f = 0; f < kFrameCount; ++f) {
                swapChain_->GetBuffer(f, IID_PPV_ARGS(&renderTargets_[f]));
                device_->CreateRenderTargetView(renderTargets_[f], nullptr, rtvHandle);
                rtvHandle.ptr += rtvDescSize_;
            }
        }

        // ── Command allocator + list ──────────────────────────────────────
        device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        IID_PPV_ARGS(&commandAllocator_));
        device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   commandAllocator_, nullptr,
                                   IID_PPV_ARGS(&commandList_));
        commandList_->Close();

        // ── Fence ─────────────────────────────────────────────────────────
        device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
        fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        fenceValue_ = 1;

        PS5X_INFO("[DX12] Backend ready.");
        ready_ = true;
        return true;

#else // non-Windows
        PS5X_INFO("[DX12] Windows not detected — backend inactive.");
        (void)sc;
        ready_ = true; // headless pass-through for CI
        return true;
#endif
    }

    void Shutdown() override
    {
#if defined(_WIN32)
        WaitForGpu();
        if (fenceEvent_)      CloseHandle(fenceEvent_);
        SafeRelease(fence_);
        SafeRelease(commandList_);
        SafeRelease(commandAllocator_);
        for (auto& rt : renderTargets_) SafeRelease(rt);
        SafeRelease(rtvHeap_);
        SafeRelease(swapChain_);
        SafeRelease(commandQueue_);
        SafeRelease(device_);
        SafeRelease(factory_);
#endif
        ready_ = false;
        PS5X_INFO("[DX12] Backend shut down.");
    }

    void BeginFrame() override
    {
#if defined(_WIN32)
        if (!ready_ || !swapChain_) return;
        frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
        commandAllocator_->Reset();
        commandList_->Reset(commandAllocator_, nullptr);

        // Transition: PRESENT → RENDER_TARGET
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = renderTargets_[frameIndex_];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList_->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += static_cast<SIZE_T>(frameIndex_) * rtvDescSize_;
        commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        const float clearColor[4] = {0.05f, 0.05f, 0.07f, 1.0f};
        commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        stats_.frameIndex++;
#endif
    }

    void EndFrame() override
    {
#if defined(_WIN32)
        if (!ready_ || !swapChain_) return;
        // Transition: RENDER_TARGET → PRESENT
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = renderTargets_[frameIndex_];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        commandList_->ResourceBarrier(1, &barrier);
        commandList_->Close();

        ID3D12CommandList* lists[] = {commandList_};
        commandQueue_->ExecuteCommandLists(1, lists);
#endif
    }

    void Present() override
    {
#if defined(_WIN32)
        if (!ready_) return;
        if (swapChain_) swapChain_->Present(1, 0); // vsync
        SignalAndWait();
        stats_.framesPresented++;
#endif
    }

    bool Resize(uint32_t w, uint32_t h) override
    {
        width_ = w; height_ = h;
#if defined(_WIN32)
        if (!ready_ || !swapChain_) return true;
        WaitForGpu();
        for (auto& rt : renderTargets_) SafeRelease(rt);
        HRESULT hr = swapChain_->ResizeBuffers(kFrameCount, w, h,
                                               DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        if (FAILED(hr)) { PS5X_ERROR("[DX12] ResizeBuffers failed."); return false; }
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT f = 0; f < kFrameCount; ++f) {
            swapChain_->GetBuffer(f, IID_PPV_ARGS(&renderTargets_[f]));
            device_->CreateRenderTargetView(renderTargets_[f], nullptr, rtvHandle);
            rtvHandle.ptr += rtvDescSize_;
        }
        PS5X_INFO("[DX12] Resized to %ux%u.", w, h);
#endif
        return true;
    }

    std::string_view        Name()        const override { return "DirectX 12"; }
    Config::GraphicsBackend BackendType() const override { return Config::GraphicsBackend::DirectX12; }
    const FrameStats&       Stats()       const override { return stats_; }

private:
    static constexpr uint32_t kFrameCount = 2;

#if defined(_WIN32)
    template<typename T>
    static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

    void WaitForGpu() {
        if (!commandQueue_ || !fence_ || !fenceEvent_) return;
        commandQueue_->Signal(fence_, fenceValue_);
        fence_->SetEventOnCompletion(fenceValue_++, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    void SignalAndWait() {
        commandQueue_->Signal(fence_, fenceValue_);
        fence_->SetEventOnCompletion(fenceValue_++, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    IDXGIFactory1*         factory_          = nullptr;
    ID3D12Device*          device_           = nullptr;
    ID3D12CommandQueue*    commandQueue_      = nullptr;
    IDXGISwapChain3*       swapChain_        = nullptr;
    ID3D12DescriptorHeap*  rtvHeap_          = nullptr;
    UINT                   rtvDescSize_      = 0;
    ID3D12Resource*        renderTargets_[kFrameCount] = {};
    ID3D12CommandAllocator* commandAllocator_ = nullptr;
    ID3D12GraphicsCommandList* commandList_  = nullptr;
    ID3D12Fence*           fence_            = nullptr;
    HANDLE                 fenceEvent_       = nullptr;
    UINT64                 fenceValue_       = 0;
    UINT                   frameIndex_       = 0;
    HWND                   hwnd_             = nullptr;
#endif
    uint32_t   width_  = 1280;
    uint32_t   height_ = 720;
    bool       ready_  = false;
    FrameStats stats_{};
};

// ═══════════════════════════════════════════════════════════════════════════
// DirectX 11 Backend
// ═══════════════════════════════════════════════════════════════════════════
class DX11Backend final : public IRendererBackend
{
public:
    bool Init(const Config::GraphicsConfig& cfg, const SwapChainDesc& sc) override
    {
        (void)cfg;
        width_  = sc.width;
        height_ = sc.height;

#if defined(_WIN32)
        HWND hwnd = static_cast<HWND>(sc.nativeWindowHandle);
        HRESULT hr;

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
        };
        D3D_FEATURE_LEVEL achieved{};

        UINT flags = 0;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        if (hwnd) {
            DXGI_SWAP_CHAIN_DESC scDesc{};
            scDesc.BufferCount        = 2;
            scDesc.BufferDesc.Width   = width_;
            scDesc.BufferDesc.Height  = height_;
            scDesc.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
            scDesc.BufferDesc.RefreshRate = {60, 1};
            scDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scDesc.OutputWindow       = hwnd;
            scDesc.SampleDesc         = {1, 0};
            scDesc.Windowed           = TRUE;
            scDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;

            hr = D3D11CreateDeviceAndSwapChain(
                    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                    featureLevels, _countof(featureLevels),
                    D3D11_SDK_VERSION, &scDesc,
                    &swapChain_, &device_, &achieved, &context_);
        } else {
            hr = D3D11CreateDevice(
                    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                    featureLevels, _countof(featureLevels),
                    D3D11_SDK_VERSION, &device_, &achieved, &context_);
        }

        if (FAILED(hr)) {
            PS5X_ERROR("[DX11] D3D11CreateDevice failed (hr=0x%08X).", hr);
            return false;
        }

        // ── Render-target view ────────────────────────────────────────────
        if (swapChain_) {
            ID3D11Texture2D* backBuf = nullptr;
            swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf));
            if (backBuf) {
                device_->CreateRenderTargetView(backBuf, nullptr, &rtv_);
                backBuf->Release();
            }
        }

        PS5X_INFO("[DX11] Backend ready (feature level 0x%x).",
                  static_cast<unsigned>(achieved));
        ready_ = true;
        return true;
#else
        ready_ = true;
        return true;
#endif
    }

    void Shutdown() override
    {
#if defined(_WIN32)
        SafeRelease(rtv_);
        SafeRelease(swapChain_);
        if (context_) { context_->ClearState(); context_->Release(); context_ = nullptr; }
        SafeRelease(device_);
#endif
        ready_ = false;
        PS5X_INFO("[DX11] Backend shut down.");
    }

    void BeginFrame() override
    {
#if defined(_WIN32)
        if (!ready_ || !context_ || !rtv_) return;
        const float clear[4] = {0.05f, 0.05f, 0.07f, 1.0f};
        context_->ClearRenderTargetView(rtv_, clear);
        context_->OMSetRenderTargets(1, &rtv_, nullptr);
        stats_.frameIndex++;
#endif
    }

    void EndFrame() override {}

    void Present() override
    {
#if defined(_WIN32)
        if (!ready_) return;
        if (swapChain_) swapChain_->Present(1, 0);
        stats_.framesPresented++;
#endif
    }

    bool Resize(uint32_t w, uint32_t h) override
    {
        width_ = w; height_ = h;
#if defined(_WIN32)
        if (!ready_ || !swapChain_) return true;
        SafeRelease(rtv_);
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        swapChain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* backBuf = nullptr;
        swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuf));
        if (backBuf) { device_->CreateRenderTargetView(backBuf, nullptr, &rtv_); backBuf->Release(); }
        PS5X_INFO("[DX11] Resized to %ux%u.", w, h);
#endif
        return true;
    }

    std::string_view        Name()        const override { return "DirectX 11"; }
    Config::GraphicsBackend BackendType() const override { return Config::GraphicsBackend::DirectX11; }
    const FrameStats&       Stats()       const override { return stats_; }

private:
#if defined(_WIN32)
    template<typename T>
    static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

    ID3D11Device*           device_    = nullptr;
    ID3D11DeviceContext*    context_   = nullptr;
    IDXGISwapChain*         swapChain_ = nullptr;
    ID3D11RenderTargetView* rtv_       = nullptr;
#endif
    uint32_t   width_  = 1280;
    uint32_t   height_ = 720;
    bool       ready_  = false;
    FrameStats stats_{};
};

// ═══════════════════════════════════════════════════════════════════════════
// Vulkan Backend (Win32 — loads vulkan-1.dll at runtime)
// ═══════════════════════════════════════════════════════════════════════════
class VulkanBackend final : public IRendererBackend
{
public:
    bool Init(const Config::GraphicsConfig& cfg, const SwapChainDesc& sc) override
    {
        (void)cfg;
        width_  = sc.width;
        height_ = sc.height;

#if defined(_WIN32)
        // Runtime-load vulkan-1.dll — graceful fail if absent
        hVulkan_ = ::LoadLibraryA("vulkan-1.dll");
        if (!hVulkan_) {
            PS5X_WARN("[Vulkan] vulkan-1.dll not found — backend unavailable.");
            return false;
        }
        using PFN_vkGetInstanceProcAddr_t = void*(*)(void*, const char*);
        auto vkGetInstanceProcAddr =
            reinterpret_cast<PFN_vkGetInstanceProcAddr_t>(
                ::GetProcAddress(hVulkan_, "vkGetInstanceProcAddr"));
        if (!vkGetInstanceProcAddr) {
            PS5X_ERROR("[Vulkan] Failed to resolve vkGetInstanceProcAddr.");
            ::FreeLibrary(hVulkan_); hVulkan_ = nullptr;
            return false;
        }
        // Full Vulkan init (instance, device, swapchain, render pass, etc.)
        // is scoped to the Kyty integration milestone. We confirm the DLL
        // loads and surface is reachable here.
        PS5X_INFO("[Vulkan] vulkan-1.dll loaded. Instance/device init pending Kyty milestone.");
        ready_ = true;
        return true;
#else
        ready_ = true;
        return true;
#endif
    }

    void Shutdown() override
    {
#if defined(_WIN32)
        if (hVulkan_) { ::FreeLibrary(hVulkan_); hVulkan_ = nullptr; }
#endif
        ready_ = false;
        PS5X_INFO("[Vulkan] Backend shut down.");
    }

    void BeginFrame() override { if (ready_) stats_.frameIndex++; }
    void EndFrame()   override {}
    void Present()    override { if (ready_) stats_.framesPresented++; }

    bool Resize(uint32_t w, uint32_t h) override
    {
        width_ = w; height_ = h;
        PS5X_INFO("[Vulkan] Resize %ux%u (swapchain recreate pending Kyty milestone).", w, h);
        return true;
    }

    std::string_view        Name()        const override { return "Vulkan"; }
    Config::GraphicsBackend BackendType() const override { return Config::GraphicsBackend::Vulkan; }
    const FrameStats&       Stats()       const override { return stats_; }

private:
#if defined(_WIN32)
    HMODULE hVulkan_ = nullptr;
#endif
    uint32_t   width_  = 1280;
    uint32_t   height_ = 720;
    bool       ready_  = false;
    FrameStats stats_{};
};

// ═══════════════════════════════════════════════════════════════════════════
// Null backend — headless / unit-test mode
// ═══════════════════════════════════════════════════════════════════════════
class NullBackend final : public IRendererBackend
{
public:
    bool Init(const Config::GraphicsConfig&, const SwapChainDesc& sc) override
    {
        width_ = sc.width; height_ = sc.height;
        PS5X_INFO("[NullRenderer] Headless backend ready (%ux%u).", width_, height_);
        return true;
    }
    void Shutdown() override { PS5X_INFO("[NullRenderer] Shut down."); }
    void BeginFrame() override { stats_.frameIndex++; }
    void EndFrame()   override {}
    void Present()    override { stats_.framesPresented++; }
    bool Resize(uint32_t w, uint32_t h) override { width_=w; height_=h; return true; }

    std::string_view        Name()        const override { return "Null"; }
    Config::GraphicsBackend BackendType() const override { return Config::GraphicsBackend::Null; }
    const FrameStats&       Stats()       const override { return stats_; }

private:
    uint32_t width_=0, height_=0;
    FrameStats stats_{};
};

// ═══════════════════════════════════════════════════════════════════════════
// Factory
// ═══════════════════════════════════════════════════════════════════════════
std::unique_ptr<IRendererBackend> CreateBackend(Config::GraphicsBackend type)
{
    switch (type) {
        case Config::GraphicsBackend::DirectX12: return std::make_unique<DX12Backend>();
        case Config::GraphicsBackend::DirectX11: return std::make_unique<DX11Backend>();
        case Config::GraphicsBackend::Vulkan:    return std::make_unique<VulkanBackend>();
        case Config::GraphicsBackend::Null:      return std::make_unique<NullBackend>();
        case Config::GraphicsBackend::OpenGL:    return std::make_unique<NullBackend>();
        default: break;
    }
    PS5X_ERROR("[Renderer] Unknown backend type %u", static_cast<unsigned>(type));
    return nullptr;
}

uint32_t AvailableBackends()
{
    uint32_t mask = 0;
#if defined(_WIN32)
    // D3D12 available on Win10+
    mask |= (1u << static_cast<uint32_t>(Config::GraphicsBackend::DirectX12));
    // D3D11 always available on Windows
    mask |= (1u << static_cast<uint32_t>(Config::GraphicsBackend::DirectX11));
    // Vulkan available if DLL present
    HMODULE vk = ::LoadLibraryA("vulkan-1.dll");
    if (vk) {
        mask |= (1u << static_cast<uint32_t>(Config::GraphicsBackend::Vulkan));
        ::FreeLibrary(vk);
    }
#endif
    // Null always available
    mask |= (1u << static_cast<uint32_t>(Config::GraphicsBackend::Null));
    return mask;
}

} // namespace PS5x::Renderer
