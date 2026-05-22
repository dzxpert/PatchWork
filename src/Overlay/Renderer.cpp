#include "Renderer.h"
#include "DX11Hook.h"
#include "UI.h"
#include "JetBrainsMonoFont.h"

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

    // Rounding — slightly more generous for a softer, modern feel
    style.WindowRounding = 8.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 5.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;

    // Spacing — more generous for breathing room
    style.FramePadding = ImVec2(10.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 5.0f);
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 8.0f;
    style.IndentSpacing = 20.0f;

    // Border — subtle cyan-tinted glow border
    style.WindowBorderSize = 1.5f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.SeparatorTextBorderSize = 1.0f;

    // Anti-aliased lines
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;

    ImVec4* colors = style.Colors;

    // ── Base palette ────────────────────────────────
    // Background: #0D1117     Surface: #161B22
    // Border:     #30363D     Text:    #E6EDF3
    // Accent:     #00D4FF     Success: #39FF14
    // Error:      #FF4444     Warning: #FFD700

    // Window
    colors[ImGuiCol_WindowBg]           = ImVec4(0.051f, 0.067f, 0.090f, 0.96f);   // #0D1117
    colors[ImGuiCol_ChildBg]            = ImVec4(0.075f, 0.092f, 0.118f, 1.0f);    // slightly lighter
    colors[ImGuiCol_PopupBg]            = ImVec4(0.075f, 0.092f, 0.118f, 0.98f);
    colors[ImGuiCol_Border]             = ImVec4(0.0f, 0.50f, 0.65f, 0.25f);       // cyan-tinted border
    colors[ImGuiCol_BorderShadow]       = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Text
    colors[ImGuiCol_Text]               = ImVec4(0.902f, 0.929f, 0.953f, 1.0f);    // #E6EDF3
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.40f, 0.43f, 0.47f, 1.0f);

    // Frame backgrounds — slightly more contrast
    colors[ImGuiCol_FrameBg]            = ImVec4(0.08f, 0.10f, 0.13f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.11f, 0.14f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.14f, 0.18f, 0.22f, 1.0f);

    // Title bar — dark with subtle cyan on active
    colors[ImGuiCol_TitleBg]            = ImVec4(0.040f, 0.050f, 0.070f, 1.0f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.050f, 0.070f, 0.095f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.040f, 0.050f, 0.070f, 0.5f);

    colors[ImGuiCol_MenuBarBg]          = ImVec4(0.065f, 0.082f, 0.105f, 1.0f);

    // Scrollbar — thin and translucent
    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.040f, 0.050f, 0.070f, 0.4f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.15f, 0.18f, 0.22f, 0.8f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.0f, 0.60f, 0.80f, 0.5f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.0f, 0.75f, 0.95f, 0.7f);

    // Accent colors (cyan) — brighter hover states
    colors[ImGuiCol_CheckMark]          = ImVec4(0.0f, 0.831f, 1.0f, 1.0f);        // #00D4FF
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.0f, 0.831f, 1.0f, 0.75f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.0f, 0.900f, 1.0f, 1.0f);

    // Buttons — subtle cyan glow
    colors[ImGuiCol_Button]             = ImVec4(0.0f, 0.831f, 1.0f, 0.12f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.0f, 0.831f, 1.0f, 0.28f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.0f, 0.831f, 1.0f, 0.45f);

    // Headers
    colors[ImGuiCol_Header]             = ImVec4(0.0f, 0.831f, 1.0f, 0.12f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.0f, 0.831f, 1.0f, 0.25f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.0f, 0.831f, 1.0f, 0.38f);

    // Separator — cyan-tinted
    colors[ImGuiCol_Separator]          = ImVec4(0.0f, 0.40f, 0.55f, 0.25f);
    colors[ImGuiCol_SeparatorHovered]   = ImVec4(0.0f, 0.831f, 1.0f, 0.5f);
    colors[ImGuiCol_SeparatorActive]    = ImVec4(0.0f, 0.831f, 1.0f, 1.0f);

    // Tabs — brighter active state
    colors[ImGuiCol_Tab]                = ImVec4(0.065f, 0.082f, 0.105f, 1.0f);
    colors[ImGuiCol_TabHovered]         = ImVec4(0.0f, 0.831f, 1.0f, 0.28f);
    colors[ImGuiCol_TabActive]          = ImVec4(0.0f, 0.55f, 0.75f, 0.30f);
    colors[ImGuiCol_TabUnfocused]       = ImVec4(0.040f, 0.050f, 0.070f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.065f, 0.082f, 0.105f, 1.0f);

    // Resize grip
    colors[ImGuiCol_ResizeGrip]         = ImVec4(0.0f, 0.831f, 1.0f, 0.08f);
    colors[ImGuiCol_ResizeGripHovered]  = ImVec4(0.0f, 0.831f, 1.0f, 0.35f);
    colors[ImGuiCol_ResizeGripActive]   = ImVec4(0.0f, 0.831f, 1.0f, 0.65f);

    // Selection and navigation
    colors[ImGuiCol_TextSelectedBg]     = ImVec4(0.0f, 0.831f, 1.0f, 0.22f);
    colors[ImGuiCol_NavHighlight]       = ImVec4(0.0f, 0.831f, 1.0f, 0.80f);

    // Modal dim background
    colors[ImGuiCol_ModalWindowDimBg]   = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
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

    // --- Layout-aware character input ---
    // Build keyboard state manually from GetAsyncKeyState since
    // GetKeyboardState() is empty on the render/hook thread (no message pump).
    BYTE keyboardState[256] = {};
    for (int k = 0; k < 256; k++)
    {
        if (GetAsyncKeyState(k) & 0x8000)
            keyboardState[k] = 0x80;
    }
    // Toggle state for caps lock / num lock
    if (GetKeyState(VK_CAPITAL) & 1) keyboardState[VK_CAPITAL] |= 0x01;
    if (GetKeyState(VK_NUMLOCK) & 1) keyboardState[VK_NUMLOCK] |= 0x01;

    HKL layout = GetKeyboardLayout(0);

    // Update ImGuiKey events for alphanumeric keys (needed for Ctrl+shortcuts)
    for (int i = 0x30; i <= 0x39; ++i)
        update_key((ImGuiKey)(ImGuiKey_0 + (i - 0x30)), i);
    for (int i = 0x41; i <= 0x5A; ++i)
        update_key((ImGuiKey)(ImGuiKey_A + (i - 0x41)), i);

    // All virtual keys that can produce printable characters
    static const int charVKeys[] = {
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
        0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54,
        0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
        VK_OEM_1, VK_OEM_2, VK_OEM_3, VK_OEM_4, VK_OEM_5, VK_OEM_6, VK_OEM_7, VK_OEM_8,
        VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_MINUS, VK_OEM_PLUS, VK_OEM_102,
        VK_SPACE,
    };
    static const int numCharVKeys = sizeof(charVKeys) / sizeof(charVKeys[0]);

    // Edge detection + key repeat: track state per VK
    static bool  prevHeld[256] = {};
    static float holdTime[256] = {};
    const float  repeatDelay  = 0.30f; // initial delay before repeat starts
    const float  repeatRate   = 0.033f; // ~30 chars/sec while held
    float dt = io.DeltaTime;

    for (int idx = 0; idx < numCharVKeys; idx++)
    {
        int vk = charVKeys[idx];
        bool held = (GetAsyncKeyState(vk) & 0x8000) != 0;

        bool fire = false;
        if (held && !prevHeld[vk])
        {
            fire = true;                   // key just pressed
            holdTime[vk] = 0.0f;
        }
        else if (held)
        {
            holdTime[vk] += dt;
            if (holdTime[vk] >= repeatDelay)
            {
                holdTime[vk] -= repeatRate; // fire repeats at repeatRate
                fire = true;
            }
        }
        else
        {
            holdTime[vk] = 0.0f;
        }
        prevHeld[vk] = held;

        if (!fire)
            continue;

        // Don't generate characters when Ctrl is held (shortcuts like Ctrl+C)
        if (keyboardState[VK_CONTROL] & 0x80)
            continue;

        UINT scanCode = MapVirtualKeyEx(vk, MAPVK_VK_TO_VSC, layout);
        wchar_t buf[4] = {};
        int result = ToUnicodeEx(vk, scanCode, keyboardState, buf, 4, 0, layout);

        if (result > 0)
        {
            for (int c = 0; c < result; c++)
                io.AddInputCharacterUTF16(buf[c]);
        }
        else if (result == -1)
        {
            // Dead key (e.g. ^ on AZERTY) — consume it, the next key press
            // will combine with it via ToUnicodeEx's internal state.
            // Flush the dead key state by calling ToUnicodeEx again with a dummy.
            wchar_t dummy[4] = {};
            ToUnicodeEx(vk, scanCode, keyboardState, dummy, 4, 0, layout);
        }
    }
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
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

            // Load JetBrains Mono font (embedded in binary)
            // Must be done after backend init so the font texture gets created properly
            ImFontConfig fontConfig;
            fontConfig.FontDataOwnedByAtlas = false; // data is static, don't let ImGui free it
            fontConfig.OversampleH = 2;
            fontConfig.OversampleV = 1;
            fontConfig.PixelSnapH = true;
            io.Fonts->AddFontFromMemoryTTF(
                (void*)JetBrainsMono_ttf_data,
                JetBrainsMono_ttf_size,
                15.0f,
                &fontConfig);
            io.Fonts->Build();

            // Force the DX11 backend to recreate the font texture from the new atlas
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();

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
