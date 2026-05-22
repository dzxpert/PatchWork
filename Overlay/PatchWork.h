#pragma once
#include <windows.h>

namespace PatchWork {
    void Initialize(HMODULE hModule);
    void Shutdown();
    bool IsInitialized();
    HMODULE GetModule();
}
