# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

PatchWork is a Windows DLL that exposes memory patching, function hooking, detouring, and AOB scanning to Lua scripts at runtime. It compiles to two different DLLs depending on target platform:

- **Win32 → `RPS.dll`**: A Lua C module loaded via `require("RPS")` from an existing Lua environment. Supports x86 calling conventions (cdecl, thiscall, stdcall).
- **x64 → `PatchWork.dll`**: An injectable DLL with a built-in ImGui overlay (DX11 hook), in-process Lua console, and script runner. Intended to be injected into a 64-bit target process.

## Building

Open `PatchWork.sln` in Visual Studio 2022 (toolset v143 required). The project uses MASM for the x64 assembly file, so the MASM build customization must be installed.

From the command line:
```
# Win32 builds (RPS.dll) - used by CI and tests
msbuild /m /p:Platform=x86 /p:Configuration=Debug PatchWork.sln
msbuild /m /p:Platform=x86 /p:Configuration=Release PatchWork.sln

# x64 builds (PatchWork.dll)
msbuild /m /p:Platform=x64 /p:Configuration=Debug PatchWork.sln
msbuild /m /p:Platform=x64 /p:Configuration=Release PatchWork.sln
```

Submodules must be initialized (`git submodule update --init --recursive`) before building x64, as it depends on `vendor/imgui` and `vendor/minhook`.

NuGet packages (Lua for Win32) must be restored before building: `nuget restore`.

## Running tests

Tests run against the Win32 Release build only and use Lua 5.4:

```powershell
.\tests\run.ps1
```

The script auto-installs Lua 5.4.6 from NuGet and copies `Release\RPS.dll` if not already present. Tests use the [lunatest](tests/lunatest/) framework. Test files are `tests/test-*.lua`.

## Architecture

### Core layer (both platforms): `*.cpp / *.h` in root

- **`PatchWork.h/.cpp`** — Public C++ API (`RPS_initialize`, `RPS_initializeLuaAPI`, `RPS_executeSnippet`, etc.) and the `RPS_LIB` Lua function registration table. Also implements `luaopen_RPS` so the DLL can be loaded via `require("RPS")`. Export macro: `PATCHWORK_API` (controlled by `PATCHWORKLIBRARY_EXPORTS` preprocessor define).
- **`CodeFunctions.h/.cpp`** — Heart of the hooking engine. Contains `LuaHook` (function hooking) and `LuaDetour` (code detouring) classes, `DoCreateCallHook` (installs the trampoline), `luaHookCode`, `luaExposeCode`, `luaDetourCode`, and `luaCallMachineCode`. x86 uses inline `__asm`; x64 delegates to MASM stubs.
- **`CodeFunctions_x64.asm`** — MASM assembly for x64: `CallMachineCode_x64` (dynamically marshals args into Windows x64 ABI), `LuaLandingFromCpp_x64` (hook trampoline), `detourLandingFunction_x64` (detour trampoline).
- **`MemoryFunctions.h/.cpp`** — Lua-callable memory R/W: `readByte/SmallInteger/Integer/String/Bytes`, `writeByte/…`, `allocate/deallocate`, `copyMemory`, `setMemory`.
- **`AOB.h/.cpp`** — `AOB::FindInRange`: scans process memory for a hex+wildcard byte pattern.
- **`LibraryFunctions.h/.cpp`** — `loadLibraryA`, `getLibraryProcAddressA`, `getProcAddress` wrappers.
- **`Memory.h`** — `ProcessMemory` singleton: owns the executable heap (`HeapCreate(HEAP_CREATE_ENABLE_EXECUTE)`) used by `allocateCode` and hook trampolines.
- **`UtilityFunctions.h/.cpp`** — Shared helpers; defines the global `LC` (lua_State*).

### Overlay layer (x64 only): `Overlay/`

Activated via `dllmain.cpp` → `PatchWork::Initialize` on `DLL_PROCESS_ATTACH`. Depends on MinHook (hook management) and Dear ImGui (rendering via DX11).

- **`Overlay/PatchWork.h/.cpp`** — Top-level coordinator: initializes MinHook, installs DX11 hooks, starts LuaEngine, sets up FileManager and UI.
- **`Overlay/DX11Hook.cpp`** — Hooks `IDXGISwapChain::Present` and `ResizeBuffers` using MinHook to inject ImGui rendering each frame.
- **`Overlay/Renderer.cpp`** — Wraps ImGui frame begin/end; applies the `ApplyPatchWorkTheme` color scheme.
- **`Overlay/UI.cpp`** — ImGui window: INSERT key toggles visibility; tabs for Console and Script management.
- **`Overlay/LuaEngine.cpp`** — Manages the in-overlay Lua state; calls `RPS_setLuaState` and `RPS_initializeLuaAPI` to wire in the RPS API; overrides `print` to route output to the Console; sets `package.path` to `C:\PatchWork\scripts\`.
- **`Overlay/Console.cpp`** — Colored line buffer displayed in the ImGui overlay.
- **`Overlay/ScriptTab.h`** — `ScriptTab` struct for the script editor tab state.
- **`Overlay/FileManager.cpp`** — Creates and manages the `C:\PatchWork\` directory hierarchy for user scripts and session persistence.

### Platform-specific hook dispatch

The x86 hook path uses a set of C-linkage globals (`luaHookedFunctionArgCount`, `luaCallingConvention`, `currentECXValue`, etc.) that are written by `SetLuaHookedFunctionParameters` and read by the naked `LuaLandingFromCpp` assembly thunk to dispatch to `executeLuaHook`. The x64 path eliminates calling-convention complexity: all functions use the Windows x64 ABI, and `CallMachineCode_x64` dynamically loads args into RCX/RDX/R8/R9 plus stack.

### NuGet packaging (Win32 only)

`PatchWork.nuspec` packages `RPS.dll` + headers for consumers. `RPS.targets` and `RPS-propertiesui.xml` wire PatchWork into consuming MSBuild projects. Published to GitHub Packages on `refs/tags/v*` tags.

## Key constraints

- x86 builds must not reference anything in `Overlay/` — those files are x64-only and depend on D3D11/ImGui/MinHook.
- The `MASM` build customization (`masm.props`/`masm.targets`) must be imported for the x64 build to compile `CodeFunctions_x64.asm`.
- Hook trampolines on x86 need at least 5 bytes (`JMP_OR_CALL_REQUIRED_BYTE_COUNT = 5`); x64 needs 14 bytes for the absolute jump.
- `hookMapping` and `detourTargetMap` are process-global maps — only one hook/detour is allowed per address.
- The argument limit for hooks/exposed functions is 20 (`RPS_ARGUMENT_LIMIT`).
- The Lua module name stays `RPS` (`luaopen_RPS`, `require("RPS")`) for backward compatibility with existing scripts and tests.
