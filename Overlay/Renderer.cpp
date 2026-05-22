#include "Renderer.h"
#include "DX11Hook.h"
#include "UI.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <chrono>

// Forward declaration for ImGui Win32 backend
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// DX11 global state definitions
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
HWND g_hwnd = nullptr;
bool g_ShowOverlay = true;

void ApplyPatchWorkTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounding
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;

    // Spacing
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;

    // Border
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    ImVec4* colors = style.Colors;

    // Background: #0D1117
    // Surface:    #161B22
    // Border:     #30363D
    // Text:       #E6EDF3
    // Accent:     #00D4FF (cyan)
    // Success:    #39FF14 (green)
    // Error:      #FF4444 (red)
    // Warning:    #FFD700 (gold)

    colors[ImGuiCol_WindowBg]           = ImVec4(0.051f, 0.067f, 0.090f, 0.95f);   // #0D1117
    colors[ImGuiCol_ChildBg]            = ImVec4(0.086f, 0.106f, 0.133f, 1.0f);    // #161B22
    colors[ImGuiCol_PopupBg]            = ImVec4(0.086f, 0.106f, 0.133f, 0.98f);
    colors[ImGuiCol_Border]             = ImVec4(0.188f, 0.212f, 0.239f, 0.6f);    // #30363D
    colors[ImGuiCol_BorderShadow]       = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_Text]               = ImVec4(0.902f, 0.929f, 0.953f, 1.0f);    // #E6EDF3
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

    colors[ImGuiCol_FrameBg]            = ImVec4(0.110f, 0.133f, 0.165f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.140f, 0.170f, 0.200f, 1.0f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.170f, 0.200f, 0.240f, 1.0f);

    colors[ImGuiCol_TitleBg]            = ImVec4(0.051f, 0.067f, 0.090f, 1.0f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.086f, 0.106f, 0.133f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.051f, 0.067f, 0.090f, 0.5f);

    colors[ImGuiCol_MenuBarBg]          = ImVec4(0.086f, 0.106f, 0.133f, 1.0f);

    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.051f, 0.067f, 0.090f, 0.5f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.188f, 0.212f, 0.239f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.280f, 0.310f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.300f, 0.330f, 0.360f, 1.0f);

    // Accent colors (cyan)
    colors[ImGuiCol_CheckMark]          = ImVec4(0.0f, 0.831f, 1.0f, 1.0f);        // #00D4FF
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.0f, 0.831f, 1.0f, 0.8f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.0f, 0.900f, 1.0f, 1.0f);

    colors[ImGuiCol_Button]             = ImVec4(0.0f, 0.831f, 1.0f, 0.15f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.0f, 0.831f, 1.0f, 0.30f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.0f, 0.831f, 1.0f, 0.50f);

    colors[ImGuiCol_Header]             = ImVec4(0.0f, 0.831f, 1.0f, 0.15f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.0f, 0.831f, 1.0f, 0.30f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.0f, 0.831f, 1.0f, 0.40f);

    colors[ImGuiCol_Separator]          = ImVec4(0.188f, 0.212f, 0.239f, 0.6f);
    colors[ImGuiCol_SeparatorHovered]   = ImVec4(0.0f, 0.831f, 1.0f, 0.5f);
    colors[ImGuiCol_SeparatorActive]    = ImVec4(0.0f, 0.831f, 1.0f, 1.0f);

    colors[ImGuiCol_Tab]                = ImVec4(0.086f, 0.106f, 0.133f, 1.0f);
    colors[ImGuiCol_TabHovered]         = ImVec4(0.0f, 0.831f, 1.0f, 0.30f);
    colors[ImGuiCol_TabActive]          = ImVec4(0.0f, 0.831f, 1.0f, 0.20f);
    colors[ImGuiCol_TabUnfocused]       = ImVec4(0.051f, 0.067f, 0.090f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.086f, 0.106f, 0.133f, 1.0f);

    colors[ImGuiCol_ResizeGrip]         = ImVec4(0.0f, 0.831f, 1.0f, 0.10f);
    colors[ImGuiCol_ResizeGripHovered]  = ImVec4(0.0f, 0.831f, 1.0f, 0.40f);
    colors[ImGuiCol_ResizeGripActive]   = ImVec4(0.0f, 0.831f, 1.0f, 0.70f);

    colors[ImGuiCol_TextSelectedBg]     = ImVec4(0.0f, 0.831f, 1.0f, 0.25f);
    colors[ImGuiCol_NavHighlight]       = ImVec4(0.0f, 0.831f, 1.0f, 0.80f);
}

// Input polling via GetAsyncKeyState (no WndProc hooking)
// Based on Imperator's PollInput pattern
static void PollInput(HWND hWnd)
{
    ImGuiIO& io = ImGui::GetIO();

    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    // DeltaTime
    static auto last_time = std::chrono::high_resolution_clock::now();
    auto current_time = std::chrono::high_resolution_clock::now();
    float delta_time = std::chrono::duration<float>(current_time - last_time).count();
    io.DeltaTime = delta_time > 0.f ? delta_time : 0.00001f;
    last_time = current_time;

    // Display size
    RECT rect;
    if (GetClientRect(hWnd, &rect))
        io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

    // Mouse position
    POINT mouse_pos;
    GetCursorPos(&mouse_pos);
    ScreenToClient(hWnd, &mouse_pos);
    io.AddMousePosEvent((float)mouse_pos.x, (float)mouse_pos.y);

    // Mouse buttons
    io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);

    // Cursor visibility
    io.MouseDrawCursor = g_ShowOverlay;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // INSERT toggle (edge-triggered)
    static bool insert_pressed = false;
    bool insert_down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (insert_down && !insert_pressed)
        g_ShowOverlay = !g_ShowOverlay;
    insert_pressed = insert_down;

    // Only process keyboard when overlay is shown
    if (!g_ShowOverlay) return;

    // Helper to update key state
    auto update_key = [&](ImGuiKey key, int vkey) {
        io.AddKeyEvent(key, (GetAsyncKeyState(vkey) & 0x8000) != 0);
    };

    // Modifiers
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    update_key(ImGuiMod_Shift, VK_SHIFT);
    update_key(ImGuiMod_Ctrl, VK_CONTROL);
    update_key(ImGuiMod_Alt, VK_MENU);

    // Navigation & editing keys
    update_key(ImGuiKey_UpArrow, VK_UP);
    update_key(ImGuiKey_DownArrow, VK_DOWN);
    update_key(ImGuiKey_LeftArrow, VK_LEFT);
    update_key(ImGuiKey_RightArrow, VK_RIGHT);
    update_key(ImGuiKey_Enter, VK_RETURN);
    update_key(ImGuiKey_Escape, VK_ESCAPE);
    update_key(ImGuiKey_Backspace, VK_BACK);
    update_key(ImGuiKey_Delete, VK_DELETE);
    update_key(ImGuiKey_Tab, VK_TAB);
    update_key(ImGuiKey_Space, VK_SPACE);
    update_key(ImGuiKey_Home, VK_HOME);
    update_key(ImGuiKey_End, VK_END);
    update_key(ImGuiKey_PageUp, VK_PRIOR);
    update_key(ImGuiKey_PageDown, VK_NEXT);

    // Alphanumeric keys (0-9)
    for (int i = 0x30; i <= 0x39; ++i)
    {
        if (GetAsyncKeyState(i) & 1)
            io.AddInputCharacter((unsigned int)i);
        update_key((ImGuiKey)(ImGuiKey_0 + (i - 0x30)), i);
    }

    // Alphanumeric keys (A-Z)
    for (int i = 0x41; i <= 0x5A; ++i)
    {
        if (GetAsyncKeyState(i) & 1)
            io.AddInputCharacter(shift ? (unsigned int)i : (unsigned int)(i + 32));
        update_key((ImGuiKey)(ImGuiKey_A + (i - 0x41)), i);
    }

    // Common symbols
    auto update_symbol = [&](int vkey, char plain, char shifted) {
        if (GetAsyncKeyState(vkey) & 1)
            io.AddInputCharacter(shift ? (unsigned int)shifted : (unsigned int)plain);
    };

    update_symbol(VK_OEM_COMMA, ',', '<');
    update_symbol(VK_OEM_PERIOD, '.', '>');
    update_symbol(VK_OEM_MINUS, '-', '_');
    update_symbol(VK_OEM_PLUS, '=', '+');
    update_symbol(VK_OEM_1, ';', ':');
    update_symbol(VK_OEM_2, '/', '?');
    update_symbol(VK_OEM_3, '`', '~');
    update_symbol(VK_OEM_4, '[', '{');
    update_symbol(VK_OEM_5, '\\', '|');
    update_symbol(VK_OEM_6, ']', '}');
    update_symbol(VK_OEM_7, '\'', '"');
}

// ============ hkPresent ============
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    static bool bInit = false;

    if (!bInit)
    {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice)))
        {
            g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);

            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            g_hwnd = sd.OutputWindow;

            // Create render target view
            ID3D11Texture2D* pBackBuffer = nullptr;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
            pBackBuffer->Release();

            // Initialize ImGui
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.IniFilename = nullptr;

            ImGui_ImplWin32_Init(g_hwnd);

            // Load default font (ImGui built-in, monospace-ish)
            // Could embed Inter Medium here in the future
            io.Fonts->AddFontDefault();
            io.Fonts->Build();

            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

            ApplyPatchWorkTheme();

            bInit = true;
        }
    }

    if (bInit)
    {
        PollInput(g_hwnd);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_ShowOverlay)
            UI::Render();

        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

// ============ hkResizeBuffers ============
HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount,
    UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    // Release current render target
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }

    // Call original
    HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    // Recreate render target from new back buffer
    ID3D11Texture2D* pBackBuffer = nullptr;
    pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (pBackBuffer)
    {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }

    return hr;
}
