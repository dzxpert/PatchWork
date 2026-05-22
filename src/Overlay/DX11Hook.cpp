#include "DX11Hook.h"
#include "Renderer.h"
#include <MinHook.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "libMinHook.x64.lib")

// Original function pointers — used by Renderer.cpp
Present_t oPresent = nullptr;
ResizeBuffers_t oResizeBuffers = nullptr;

// Forward declarations of hook functions defined in Renderer.cpp
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount,
    UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

namespace DX11Hook
{
    // Stores the vtable address for unhooking
    static void* s_presentTarget = nullptr;
    static void* s_resizeTarget = nullptr;

    bool GetD3D11DeviceVTable(void** pTable, size_t size)
    {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L,
            GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"DX11", nullptr };
        RegisterClassEx(&wc);
        HWND hwnd = CreateWindow(wc.lpszClassName, L"DX11", WS_OVERLAPPEDWINDOW,
            100, 100, 300, 300, nullptr, nullptr, wc.hInstance, nullptr);

        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_11_0 };

        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* swapChain = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;

        if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevels, 2, D3D11_SDK_VERSION, &sd, &swapChain, &device, &featureLevel, &context) == S_OK)
        {
            memcpy(pTable, *(void***)swapChain, size);
            swapChain->Release();
            device->Release();
            context->Release();
            DestroyWindow(hwnd);
            UnregisterClass(wc.lpszClassName, wc.hInstance);
            return true;
        }

        DestroyWindow(hwnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return false;
    }

    bool Install()
    {
        void* d3d11Table[205] = {};
        if (!GetD3D11DeviceVTable(d3d11Table, sizeof(d3d11Table)))
            return false;

        s_presentTarget = d3d11Table[8];
        s_resizeTarget = d3d11Table[13];

        // Hook Present (vtable index 8)
        if (MH_CreateHook(s_presentTarget, &hkPresent, reinterpret_cast<LPVOID*>(&oPresent)) != MH_OK)
            return false;

        if (MH_EnableHook(s_presentTarget) != MH_OK)
            return false;

        // Hook ResizeBuffers (vtable index 13)
        if (MH_CreateHook(s_resizeTarget, &hkResizeBuffers, reinterpret_cast<LPVOID*>(&oResizeBuffers)) != MH_OK)
            return false;

        if (MH_EnableHook(s_resizeTarget) != MH_OK)
            return false;

        return true;
    }

    void Uninstall()
    {
        MH_DisableHook(MH_ALL_HOOKS);

        if (s_presentTarget)
        {
            MH_RemoveHook(s_presentTarget);
            s_presentTarget = nullptr;
        }
        if (s_resizeTarget)
        {
            MH_RemoveHook(s_resizeTarget);
            s_resizeTarget = nullptr;
        }

        oPresent = nullptr;
        oResizeBuffers = nullptr;
    }
}
