# PatchWork [![MSBuild](https://github.com/Meshmash/PatchWork/actions/workflows/msbuild.yml/badge.svg?event=push)](https://github.com/Meshmash/PatchWork/actions/workflows/msbuild.yml)

A runtime memory patching and code hooking library with a Lua scripting API. Supports modifying, detouring, and hooking code in a running process.

Two build targets:

- **Win32 → `RPS.dll`** — A Lua C module (`require("RPS")`) for embedding in an existing Lua environment. Supports x86 calling conventions (cdecl, thiscall, stdcall).
- **x64 → `PatchWork.dll`** — An injectable DLL with a built-in ImGui overlay (DX11 hook), in-process Lua console, and script runner for 64-bit target processes.

## Usage (Win32 / Lua module)

```lua
rps = require("RPS")
rps.hookCode(...)   -- etc.
```

## Features

### Hooking functions with Lua functions

When function A has been hooked and it is called by the program, the Lua function is called instead.
The Lua function can then optionally call the original function.

```
hookCode(luaHookCallback, hookAtAddress, argumentCount, callingConvention, hookSize)
  callingConvention: 0 = cdecl, 1 = thiscall, 2 = stdcall
  Returns: the original function
```

#### Example

```lua
function functionA_hook(this, param_1, param_2)
    local result = functionA_original(this, param_1, param_2)
    if result == 1 then result = 0 end
    return result
end

functionA_original = hookCode(functionA_hook, 0xABCDEF12, 3, 1, 6)
```

### Exposing functions to Lua

```
exposeCode(address, argumentCount, callingConvention)
  Returns: a callable function
```

#### Example

```lua
functionA = exposeCode(0xABCDEF12, 3, 1)

function yourFunction()
    local result = functionA(0x12345ABC, -1, 99)
    if result == 1 then result = 0 end
    return result
end
```

### Detour code

Redirects program flow to a Lua function. The callback receives a table of all registers (x86: `EAX`–`EDI`; x64: `RAX`–`R15`) and must return the (possibly modified) table.

```
detourCode(luaCallback, address, size)
```

#### Example

```lua
function onDetour(registers)
    registers.EAX = 1
    return registers
end

detourCode(onDetour, 0xABCDEF, 7)
```

### AOB scanning

```
scanForAOB(searchPattern[, min, max])
  searchPattern: hex string with ? wildcards, e.g. "FF A1 E? B? ?? 00"
  Returns: address or nil
```

### Memory functions

```
allocate(size)           -- allocate data memory
allocateCode(size)       -- allocate executable memory
readBytes(address, n)    -- returns table of n bytes
writeBytes(address, data)
readString(address)      -- null-terminated ASCII
readInteger(address) / writeInteger(address, value)
readSmallInteger(address) / writeSmallInteger(address, value)
readByte(address) / writeByte(address, value)
copyMemory(dst, src, n)
setMemory(address, value, n)
writeCode(address, bytes) -- write code bytes with page protection handling
```

### Library functions

```
loadLibraryA(name)
getLibraryProcAddressA(module, name)
getProcAddress(name)
```

## x64 Overlay

When injected as `PatchWork.dll` into a 64-bit process, the overlay provides:

- **INSERT** — toggle the ImGui overlay
- **Ctrl+Enter** — execute the current script
- Script editor with tabs, file open/save, and session persistence
- Lua console with colored output
- Scripts and sessions stored under `C:\PatchWork\`

## Building

Requires Visual Studio 2022 (toolset v143) with the MASM build customization installed.

```
msbuild /m /p:Platform=x86 /p:Configuration=Release PatchWork.sln   # Win32 RPS.dll
msbuild /m /p:Platform=x64 /p:Configuration=Release PatchWork.sln   # x64 PatchWork.dll
```

Restore NuGet packages before the first build: `nuget restore`

Initialize submodules for x64: `git submodule update --init --recursive`

## Running tests

Tests run against the Win32 Release build using Lua 5.4:

```powershell
.\tests\run.ps1
```
