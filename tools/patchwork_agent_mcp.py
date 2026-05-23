"""
PatchWork Agent MCP Server — Standalone stdin/stdout MCP server bridge.
Exposes PatchWork DLL capabilities as native tools to any external AI agent.

Configure in your LLM client (e.g., Claude Desktop config):
{
  "mcpServers": {
    "patchwork": {
      "command": "python",
      "args": ["c:/Users/xWantedStore/Documents/GitHub/PatchWork/tools/patchwork_agent_mcp.py"]
    }
  }
}
"""

import sys
import json
import socket
import time

HOST = "127.0.0.1"
BASE_PORT = 1339

class PatchWorkBridge:
    def __init__(self):
        self.sock = None
        self.port = None

    def connect_dll(self):
        """Scan ports 1339-1348 to connect to the PatchWork DLL socket."""
        if self.sock:
            return True
        for offset in range(10):
            port = BASE_PORT + offset
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(1.5)
                s.connect((HOST, port))
                self.sock = s
                self.port = port
                # Read initial handshake from DLL server
                reader = s.makefile("r")
                reader.readline() 
                return True
            except (ConnectionRefusedError, OSError):
                continue
        return False

    def send_dll_request(self, method, params=None):
        """Send a JSON-RPC request to the PatchWork DLL and get the result."""
        if not self.connect_dll():
            return {"error": "Could not connect to PatchWork DLL. Ensure the game is running and PatchWork.dll is injected."}
        
        payload = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": 1
        }
        try:
            self.sock.sendall((json.dumps(payload) + "\n").encode())
            reader = self.sock.makefile("r")
            resp_str = reader.readline()
            if not resp_str:
                self.sock.close()
                self.sock = None
                return {"error": "DLL connection closed unexpectedly."}
            return json.loads(resp_str)
        except Exception as e:
            if self.sock:
                self.sock.close()
            self.sock = None
            return {"error": f"Socket error: {str(e)}"}

    def handle_tool_call(self, name, arguments):
        """Translate native MCP tool calls into DLL commands."""
        # 1. execute_lua
        if name == "execute_lua":
            code = arguments.get("code", "")
            resp = self.send_dll_request("execute_lua", {"code": code})
            if "error" in resp:
                return {"content": [{"type": "text", "text": f"Error: {resp['error']}"}], "isError": True}
            result = resp.get("result", {})
            is_err = result.get("isError", False)
            content = result.get("content", [{"type": "text", "text": "(executed successfully)"}])
            return {"content": content, "isError": is_err}

        # 2. get_base_address
        elif name == "get_base_address":
            module = arguments.get("module_name", None)
            lua_code = f"return getAddress({json.dumps(module) if module else 'nil'})"
            resp = self.send_dll_request("execute_lua", {"code": lua_code})
            if "error" in resp:
                return {"content": [{"type": "text", "text": f"Error: {resp['error']}"}], "isError": True}
            res = resp.get("result", {}).get("content", [{}])[0].get("text", "nil")
            return {"content": [{"type": "text", "text": f"Base Address: {res}"}]}

        # 3. read_memory
        elif name == "read_memory":
            addr = arguments.get("address")
            mem_type = arguments.get("type", "integer")
            length = arguments.get("length", 0)
            
            # Map types to PatchWork Lua API
            lua_func = "readInteger"
            if mem_type == "byte": lua_func = "readByte"
            elif mem_type == "short": lua_func = "readSmallInteger"
            elif mem_type == "string": lua_func = f"function(a) return readString(a, {length if length > 0 else 'nil'}) end"
            
            lua_code = f"return ({lua_func})({addr})"
            resp = self.send_dll_request("execute_lua", {"code": lua_code})
            if "error" in resp:
                return {"content": [{"type": "text", "text": f"Error: {resp['error']}"}], "isError": True}
            res = resp.get("result", {}).get("content", [{}])[0].get("text", "nil")
            return {"content": [{"type": "text", "text": res}]}

        # 4. write_memory
        elif name == "write_memory":
            addr = arguments.get("address")
            mem_type = arguments.get("type", "integer")
            val = arguments.get("value")
            
            lua_func = "writeInteger"
            if mem_type == "byte": lua_func = "writeByte"
            elif mem_type == "short": lua_func = "writeSmallInteger"
            elif mem_type == "string": lua_func = "writeString"
            
            # Escape strings if needed
            lua_val = json.dumps(val) if isinstance(val, str) else str(val)
            lua_code = f"({lua_func})({addr}, {lua_val}) return 'Success'"
            resp = self.send_dll_request("execute_lua", {"code": lua_code})
            if "error" in resp:
                return {"content": [{"type": "text", "text": f"Error: {resp['error']}"}], "isError": True}
            result = resp.get("result", {})
            is_err = result.get("isError", False)
            content = result.get("content", [{"type": "text", "text": "Success"}])
            return {"content": content, "isError": is_err}

        # 5. scan_aob
        elif name == "scan_aob":
            pattern = arguments.get("pattern")
            lua_code = f"return scanForAOB({json.dumps(pattern)})"
            resp = self.send_dll_request("execute_lua", {"code": lua_code})
            if "error" in resp:
                return {"content": [{"type": "text", "text": f"Error: {resp['error']}"}], "isError": True}
            res = resp.get("result", {}).get("content", [{}])[0].get("text", "nil")
            return {"content": [{"type": "text", "text": f"Found at: {res}"}]}

        # 6. get_console
        elif name == "get_console":
            count = arguments.get("count", 25)
            resp = self.send_dll_request("get_console", {"count": count})
            if "error" in resp:
                return {"content": [{"type": "text", "text": f"Error: {resp['error']}"}], "isError": True}
            res = resp.get("result", {}).get("content", [{}])[0].get("text", "")
            return {"content": [{"type": "text", "text": res}]}

        return {"content": [{"type": "text", "text": f"Unknown tool: {name}"}], "isError": True}

def main():
    bridge = PatchWorkBridge()
    
    # Process standard I/O JSON-RPC loop for MCP
    while True:
        try:
            line = sys.stdin.readline()
            if not line:
                break
            req = json.loads(line)
            req_id = req.get("id")
            method = req.get("method")
            
            # ── MCP Lifecycle Handshakes ──
            if method == "initialize":
                resp = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "protocolVersion": "2024-11-05",
                        "capabilities": {},
                        "serverInfo": {
                            "name": "patchwork-agent-bridge",
                            "version": "1.0.0"
                        }
                    }
                }
                sys.stdout.write(json.dumps(resp) + "\n")
                sys.stdout.flush()

            elif method == "tools/list":
                resp = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "tools": [
                            {
                                "name": "execute_lua",
                                "description": "Execute Lua script commands on the game's main thread.",
                                "inputSchema": {
                                    "type": "object",
                                    "properties": {
                                        "code": {"type": "string", "description": "The Lua code string to execute."}
                                    },
                                    "required": ["code"]
                                }
                            },
                            {
                                "name": "get_base_address",
                                "description": "Get the 64-bit base memory address of a module or main executable (e.g. StarCitizen.exe).",
                                "inputSchema": {
                                    "type": "object",
                                    "properties": {
                                        "module_name": {"type": "string", "description": "Module name. Omit or pass null to get the main process executable base address."}
                                    }
                                }
                            },
                            {
                                "name": "read_memory",
                                "description": "Read a value from a specified 64-bit memory address.",
                                "inputSchema": {
                                    "type": "object",
                                    "properties": {
                                        "address": {"type": "integer", "description": "The 64-bit target address pointer."},
                                        "type": {"type": "string", "enum": ["byte", "short", "integer", "string"], "description": "Type of data to read."},
                                        "length": {"type": "integer", "description": "Number of bytes to read (only applicable if type is string)."}
                                    },
                                    "required": ["address", "type"]
                                }
                            },
                            {
                                "name": "write_memory",
                                "description": "Write a value to a specified 64-bit memory address.",
                                "inputSchema": {
                                    "type": "object",
                                    "properties": {
                                        "address": {"type": "integer", "description": "The 64-bit target address pointer."},
                                        "type": {"type": "string", "enum": ["byte", "short", "integer", "string"], "description": "Type of data to write."},
                                        "value": {"type": ["integer", "string"], "description": "The value to write (integer or string)."}
                                    },
                                    "required": ["address", "type", "value"]
                                }
                            },
                            {
                                "name": "scan_aob",
                                "description": "Scan memory for an Array of Bytes signature to find static address offsets dynamically.",
                                "inputSchema": {
                                    "type": "object",
                                    "properties": {
                                        "pattern": {"type": "string", "description": "Pattern search string with hex values and '?' wildcards. E.g. '48 89 5C 24 ? 57 48 83 EC 20'"}
                                    },
                                    "required": ["pattern"]
                                }
                            },
                            {
                                "name": "get_console",
                                "description": "Retrieve the latest lines printed to the in-game ImGui overlay console.",
                                "inputSchema": {
                                    "type": "object",
                                    "properties": {
                                        "count": {"type": "integer", "description": "Number of recent lines to retrieve. Default is 25."}
                                    }
                                }
                            }
                        ]
                    }
                }
                sys.stdout.write(json.dumps(resp) + "\n")
                sys.stdout.flush()

            elif method == "tools/call":
                params = req.get("params", {})
                name = params.get("name")
                arguments = params.get("arguments", {})
                
                result = bridge.handle_tool_call(name, arguments)
                resp = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": result
                }
                sys.stdout.write(json.dumps(resp) + "\n")
                sys.stdout.flush()
                
        except Exception as e:
            # Keep stdin loop alive, logging errors gracefully
            continue

if __name__ == "__main__":
    main()
