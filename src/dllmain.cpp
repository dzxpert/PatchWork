// dllmain.cpp : Defines the entry point for the DLL application.

#include "framework.h"
#include "Overlay/PatchWork.h"

static HMODULE g_hModule = nullptr;

static DWORD WINAPI InitThread(LPVOID lpParam) {
    PatchWork::Initialize((HMODULE)lpParam);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;
        CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        PatchWork::Shutdown();
    }
    return TRUE;
}
