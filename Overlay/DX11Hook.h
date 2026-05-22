#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(__stdcall* ResizeBuffers_t)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

extern Present_t oPresent;
extern ResizeBuffers_t oResizeBuffers;

namespace DX11Hook
{
    bool Install();
    void Uninstall();
    bool GetD3D11DeviceVTable(void** pTable, size_t size);
}
