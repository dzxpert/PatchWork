---
name: patchwork-agent
description: Expose running process manipulation tools (memory read/write, pattern scans, hooks, detours, Lua scripts) inside a 64-bit target process like Star Citizen.
---

# PatchWork Agent Skill

This skill equips AI agents to interact with a running 64-bit target process (e.g. `StarCitizen.exe`) via the **PatchWork MCP Server**. Using this skill, the agent can inspect memory, deploy update-proof hooks, detour CPU registers, and execute safe Lua operations on the process's main thread.

---

## 1. Setup & Configuration

To enable this skill for your LLM client (e.g. Claude Desktop, Cursor), add the following entry to your configuration:

```json
{
  "mcpServers": {
    "patchwork": {
      "command": "python",
      "args": ["c:/Users/xWantedStore/Documents/GitHub/PatchWork/tools/patchwork_agent_mcp.py"]
    }
  }
}
```

Ensure the target process is running and `PatchWork.dll` is injected. The bridge script will automatically scan ports `1339–1348` over localhost to hook into the DLL's Winsock server.

---

## 2. Exposed Agent Tools

The MCP bridge registers the following native tools:

| Tool Name | Arguments | Description |
|---|---|---|
| `execute_lua` | `code` (string) | Executes arbitrary Lua strings on the main thread and captures printed output. |
| `get_base_address` | `module_name` (string, optional) | Gets the 64-bit base address of a loaded module or executable. |
| `read_memory` | `address` (int), `type` (string), `length` (int, optional) | Reads a `byte`, `short`, `integer`, `qword`, or `string` from memory. |
| `write_memory` | `address` (int), `type` (string), `value` (int/string) | Writes a `byte`, `short`, `integer`, `qword`, or `string` to memory. |
| `scan_aob` | `pattern` (string) | Scans for an Array of Bytes signature to find static offsets. |
| `get_console` | `count` (int, optional) | Retrieves the latest lines printed to the in-game ImGui console. |

---

## 3. Agent Task Playbooks & Examples

### Task 1: Locating Module Base Address
Always call `get_base_address` (or write Lua using `getAddress()`) to obtain the executable base rather than hardcoding static base pointers, as ASLR (Address Space Layout Randomization) will scramble addresses on each process launch:

```lua
-- Lua execute_lua call:
local base = getAddress("StarCitizen.exe")
print(string.format("Star Citizen Base: 0x%X", base))
```

### Task 2: Robust Array of Bytes (AOB) Scanning
Use pattern signatures with `scan_aob` (or `scanForAOB` in Lua) to find address offsets. This keeps your scripts working across game updates:

```lua
-- Find a function offset by scanning for its unique byte signature:
local funcAddress = scanForAOB("48 89 5C 24 ? 57 48 83 EC 20 48 8B D9")
if funcAddress then
    print(string.format("Function found at: 0x%X", funcAddress))
else
    print("Pattern signature outdated or not found.")
end
```

### Task 3: Traversing Multi-level Pointer Chains
To safely read a deep pointer chain (e.g. `base + 0x2E68000 -> +0x80 -> +0x1A8`):

```lua
local base = getAddress("StarCitizen.exe")
local basePointer = readQword(base + 0x2E68000) -- Read 64-bit pointer

if basePointer and basePointer ~= 0 then
    local level1 = readQword(basePointer + 0x80) -- Read 64-bit pointer
    if level1 and level1 ~= 0 then
        local value = readInteger(level1 + 0x1A8) -- Read final 32-bit integer value
        print("Resolved Value: " .. value)
    end
end
```

### Task 4: Hooking a Function and Modifying Results
Replace a native function at `address` with a custom Lua callback. The callback receives original arguments and can invoke the original function to query or modify its execution flow:

```lua
local base = getAddress()
local targetAddress = base + 0xABCDE -- target function offset

-- The hook callback receives identical parameters as the native caller:
function MyFunctionHook(arg1, arg2, arg3)
    -- Call the original function to get the real result
    local result = OriginalFunction(arg1, arg2, arg3)
    
    -- Manipulate results:
    if result == 1 then
        result = 999 
    end
    return result
end

-- Wire up hook (exposes hook callback, function offset, arg count, calling convention, hook length)
OriginalFunction = hookCode(MyFunctionHook, targetAddress, 3, 0, 14)
print("Function successfully hooked!")
```

### Task 5: Code Detouring (CPU Register Manipulation)
Redirect instructions at a given address to run Lua code in the middle of a function. Detour callbacks receive a table representing general-purpose registers (`RAX` to `R15`) and must return it (optionally modified):

```lua
local base = getAddress()
local detourAddress = base + 0xF1230

function OnCPUDetour(regs)
    -- Inspect or override registers:
    if regs.RAX == 0 then
        regs.RAX = 1 -- Force status flag to 1
    end
    return regs
end

detourCode(OnCPUDetour, detourAddress, 14)
print("Instruction detoured successfully!")
```

---

## 4. Best Practices for AI Agents

> [!CAUTION]
> **CRITICAL: NEVER USE UNBASED OFFSETS**
> Absolute addresses for local game module pointers change every time the game runs due to **ASLR (Address Space Layout Randomization)**. 
> - **WRONG**: `readQword(0xA5C00F0)` — This will attempt to read absolute address `0xA5C00F0` which is in low unallocated memory, causing a Lua access violation error.
> - **RIGHT**: 
>   ```lua
>   local base = getAddress("StarCitizen.exe")
>   local pGame = readQword(base + 0xA5C00F0)
>   ```
> Always retrieve the module base address via `getAddress("StarCitizen.exe")` (or `get_base_address` if calling via MCP tools) first, and add your offset to it.

1. **Avoid Pointer Truncation**: Ensure you treat address pointers as 64-bit integers (`uintptr_t`/`DWORD_PTR`). Avoid casting pointers to 32-bit `DWORD` or `int` variables.
2. **State Validation**: Always check if pointers are `nil` or `0` before attempting to read/write them to avoid Access Violations.
3. **Graceful Error Handling**: Wrap deep nested memory operations inside conditional branches or try-catch blocks to prevent game/process crashes.
4. **Use Output Capture**: Output results using Lua `print(...)` in your `execute_lua` scripts; the MCP server will capture and return them directly to you.

---

## 5. Lua API Syntax Reference

> [!IMPORTANT]
> **Case Insensitivity**: Every API function supports both **camelCase** (e.g. `readByte`) and **PascalCase** (e.g. `ReadByte`) globally. AI agents can use either style.

### Memory Read/Write Functions
* `readByte(address) -> integer` \| `ReadByte(address) -> integer`
  Reads a single byte (8-bit) from the specified 64-bit address.
* `writeByte(address, value)` \| `WriteByte(address, value)`
  Writes a single byte (8-bit) to the specified 64-bit address.
* `readSmallInteger(address) -> integer` \| `ReadSmallInteger(address) -> integer`
  Reads a short (16-bit signed integer) from the specified 64-bit address.
* `writeSmallInteger(address, value)` \| `WriteSmallInteger(address, value)`
  Writes a short (16-bit signed integer) to the specified 64-bit address.
* `readInteger(address) -> integer` \| `ReadInteger(address) -> integer`
  Reads a standard 32-bit signed integer from the specified 64-bit address.
* `writeInteger(address, value)` \| `WriteInteger(address, value)`
  Writes a standard 32-bit signed integer to the specified 64-bit address.
* `readQword(address) -> integer` \| `ReadQword(address) -> integer`
  Reads a 64-bit pointer or QWORD value from the specified 64-bit address.
* `writeQword(address, value)` \| `WriteQword(address, value)`
  Writes a 64-bit pointer or QWORD value to the specified 64-bit address.
* `readFloat(address) -> number` \| `ReadFloat(address) -> number`
  Reads a 32-bit float value from the specified 64-bit address.
* `writeFloat(address, value)` \| `WriteFloat(address, value)`
  Writes a 32-bit float value to the specified 64-bit address.
* `readDouble(address) -> number` \| `ReadDouble(address) -> number`
  Reads a 64-bit double-precision float value from the specified 64-bit address.
* `writeDouble(address, value)` \| `WriteDouble(address, value)`
  Writes a 64-bit double-precision float value to the specified 64-bit address.
* `readString(address, [maxLength]) -> string` \| `ReadString(address, [maxLength]) -> string`
  Reads a null-terminated ASCII string from the specified 64-bit address.
* `writeString(address, string)` \| `WriteString(address, string)`
  Writes an ASCII string directly into memory at the specified 64-bit address.
* `readBytes(address, count) -> table` \| `ReadBytes(address, count) -> table`
  Reads `count` bytes from the address and returns them as a 1-indexed Lua array table of byte integers.
* `writeBytes(address, table)` \| `WriteBytes(address, table)`
  Writes an array table of byte integers sequentially starting at the specified 64-bit address.

### Utility & Memory Operations
* `isValidAddress(address) -> boolean` \| `IsValidAddress(address) -> boolean`
  Returns `true` if the specified address resides within committed, readable memory. Prevents DLL access violation crashes.
* `getAddress([moduleName]) -> integer` \| `GetAddress([moduleName]) -> integer`
  Returns the 64-bit base address of the specified module. Omit or pass `nil` to get the base address of the main injected executable.
* `allocate(size) -> integer` \| `Allocate(size) -> integer`
  Allocates `size` bytes of data memory in the process space. Returns the 64-bit allocation pointer.
* `deallocate(address)` \| `Deallocate(address)`
  Frees previously allocated data memory.
* `allocateCode(size) -> integer` \| `AllocateCode(size) -> integer`
  Allocates `size` bytes of executable memory (Read-Write-Execute). Returns the 64-bit address.
* `deallocateCode(address)` \| `DeallocateCode(address)`
  Frees allocated executable memory.
* `copyMemory(dst, src, count)` \| `CopyMemory(dst, src, count)`
  Performs a native `memcpy` of `count` bytes from `src` address to `dst` address.
* `setMemory(address, value, count)` \| `SetMemory(address, value, count)`
  Performs a native `memset` of `count` bytes to `value` starting at `address`.
* `scanForAOB(pattern, [min], [max]) -> integer` \| `ScanForAOB(pattern, [min], [max]) -> integer`
  Scans for a hexadecimal AOB signature with `?` wildcards and returns its 64-bit address, or `nil`.

### Detouring & Hooking
* `hookCode(callback, address, argCount, callingConvention, hookSize) -> function` \| `HookCode(callback, address, argCount, callingConvention, hookSize) -> function`
  Hooks a native function at `address` and redirects it to the Lua `callback` function. Returns the original native function callable from Lua.
* `callOriginal(...)` \| `CallOriginal(...)`
  Helper to invoke the original function from within a hooked callback.
* `exposeCode(address, argCount, callingConvention) -> function` \| `ExposeCode(address, argCount, callingConvention) -> function`
  Exposes a native function at `address` to be callable from Lua scripts.
* `detourCode(callback, address, size)` \| `DetourCode(callback, address, size)`
  Redirects execution flow at `address` to run a Lua `callback` in the middle of a function. Callback receives and returns a table of registers (`RAX`-`R15`).

### Dynamic Loading
* `loadLibraryA(name) -> integer` \| `LoadLibraryA(name) -> integer`
  Invokes `LoadLibraryA` on the DLL module and returns its 64-bit handle.
* `getLibraryProcAddressA(module, funcName) -> integer` \| `GetLibraryProcAddressA(module, funcName) -> integer`
  Loads a DLL module and retrieves the 64-bit function pointer for the exported symbol `funcName`.
* `getProcAddress(moduleHandle, funcName) -> integer` \| `GetProcAddress(moduleHandle, funcName) -> integer`
  Retrieves a 64-bit function pointer from a loaded module handle.
