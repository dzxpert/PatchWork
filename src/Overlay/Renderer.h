#pragma once

#include <d3d11.h>
#include <dxgi.h>

// DX11 global state (defined in Renderer.cpp)
extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;
extern ID3D11RenderTargetView* g_mainRenderTargetView;
extern HWND g_hwnd;
extern bool g_ShowOverlay;

// Hook entry points (called from DX11Hook via function pointers)
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount,
    UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

// Theme setup
void ApplyPatchWorkTheme();
