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
| `read_memory` | `address` (int), `type` (string), `length` (int, optional) | Reads a `byte`, `short`, `integer`, or `string` from memory. |
| `write_memory` | `address` (int), `type` (string), `value` (int/string) | Writes a `byte`, `short`, `integer`, or `string` to memory. |
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
local basePointer = readInteger(base + 0x2E68000)

if basePointer and basePointer ~= 0 then
    local level1 = readInteger(basePointer + 0x80)
    if level1 and level1 ~= 0 then
        local value = readInteger(level1 + 0x1A8)
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

1. **Avoid Pointer Truncation**: Ensure you treat address pointers as 64-bit integers (`uintptr_t`/`DWORD_PTR`). Avoid casting pointers to 32-bit `DWORD` or `int` variables.
2. **State Validation**: Always check if pointers are `nil` or `0` before attempting to read/write them to avoid Access Violations.
3. **Graceful Error Handling**: Wrap deep nested memory operations inside conditional branches or try-catch blocks to prevent game/process crashes.
4. **Use Output Capture**: Output results using Lua `print(...)` in your `execute_lua` scripts; the MCP server will capture and return them directly to you.
