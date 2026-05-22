# PatchWork [![MSBuild](https://github.com/Meshmash/PatchWork/actions/workflows/msbuild.yml/badge.svg?event=push)](https://github.com/Meshmash/PatchWork/actions/workflows/msbuild.yml)

An injectable x64 DLL for runtime memory patching and code hooking via Lua scripts. Inject `PatchWork.dll` into a 64-bit process to get an in-process Lua console and script runner backed by an ImGui overlay.

## Features

### Hooking functions

Replaces a function at a given address with a Lua callback. The callback receives the original arguments and can optionally call through to the original.

```
hookCode(luaHookCallback, address, argumentCount, callingConvention, hookSize)
  Returns: the original function
```

#### Example

```lua
function functionA_hook(arg1, arg2, arg3)
    local result = functionA_original(arg1, arg2, arg3)
    if result == 1 then result = 0 end
    return result
end

functionA_original = hookCode(functionA_hook, 0x1400ABCDE, 3, 0, 14)
```

### Exposing functions to Lua

Makes a native function callable from Lua.

```
exposeCode(address, argumentCount, callingConvention)
  Returns: a callable function
```

#### Example

```lua
functionA = exposeCode(0x1400ABCDE, 3, 0)

local result = functionA(0x12345ABC, -1, 99)
```

### Detouring code

Redirects execution to a Lua callback at a given address. The callback receives a register table (`RAX`–`R15`) and must return it (optionally modified).

```
detourCode(luaCallback, address, size)
```

#### Example

```lua
function onDetour(registers)
    registers.RAX = 1
    return registers
end

detourCode(onDetour, 0x1400ABCDE, 14)
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
writeCode(address, bytes)
```

### Library functions

```
loadLibraryA(name)
getLibraryProcAddressA(module, name)
getProcAddress(name)
```

## Overlay

After injection, press **INSERT** to toggle the ImGui overlay.

- **Console tab** — Lua REPL with colored output; `print()` routes here
- **Scripts tab** — editor with file open/save, tab management, and session persistence
- **Ctrl+Enter** — run the current script
- Scripts and sessions stored under `C:\PatchWork\scripts\`

## Building

Requires Visual Studio 2022 (toolset v143) with the MASM build customization installed.

```
msbuild /m /p:Platform=x64 /p:Configuration=Debug   PatchWork.sln
msbuild /m /p:Platform=x64 /p:Configuration=Release PatchWork.sln
```

NuGet packages and submodules are restored automatically on first build.
