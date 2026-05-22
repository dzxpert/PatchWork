#include "PatchWork.h"
#include "DX11Hook.h"
#include "LuaEngine.h"
#include "FileManager.h"
#include "Console.h"
#include "UI.h"

#include <MinHook.h>
#pragma comment(lib, "libMinHook.x64.lib")

static HMODULE s_hModule = nullptr;
static bool s_initialized = false;

namespace PatchWork
{
    void Initialize(HMODULE hModule)
    {
        s_hModule = hModule;

        // Wait for the host process to set up its D3D device
        Sleep(3000);

        // Initialize MinHook
        if (MH_Initialize() != MH_OK)
            return;

        // Install DX11 Present/ResizeBuffers hooks
        if (!DX11Hook::Install())
        {
            MH_Uninitialize();
            return;
        }

        // Initialize Lua engine with RPS API
        LuaEngine::Init();

        // Ensure C:\PatchWork\ directories exist
        FileManager::EnsureDirectories();

        // Initialize UI (loads previous session)
        UI::Initialize();

        // Welcome message
        Console::AddLine("PatchWork v1.0 loaded", ImVec4(0.0f, 0.831f, 1.0f, 1.0f));
        Console::AddLine("Press INSERT to toggle overlay", ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        Console::AddLine("Press Ctrl+Enter to execute code", ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

        s_initialized = true;
    }

    void Shutdown()
    {
        if (!s_initialized) return;

        // Save session
        UI::Shutdown();

        // Remove DX11 hooks and shutdown ImGui
        DX11Hook::Uninstall();

        // Close Lua state
        LuaEngine::Shutdown();

        // Uninitialize MinHook
        MH_Uninitialize();

        s_initialized = false;
    }

    bool IsInitialized()
    {
        return s_initialized;
    }

    HMODULE GetModule()
    {
        return s_hModule;
    }
}
