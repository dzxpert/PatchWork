"""
PatchWork MCP Client — Python helper for interacting with the MCP server
running inside the injected DLL (localhost:1339).

Usage:
    python patchwork_mcp.py                  # interactive mode
    python patchwork_mcp.py status           # get status
    python patchwork_mcp.py exec "print(1)"  # execute lua inline
    python patchwork_mcp.py run script.lua   # execute a file
    python patchwork_mcp.py console          # get console output
    python patchwork_mcp.py clear            # clear console
    python patchwork_mcp.py scripts          # list scripts
    python patchwork_mcp.py read 0x1234 16   # read 16 bytes from addr
    python patchwork_mcp.py write 0x1234 90  # write NOP to addr
"""

import socket
import json
import sys
import time

HOST = "127.0.0.1"
PORT = 1339

class PatchWorkMCP:
    def __init__(self, host=HOST, port=PORT):
        self.host = host
        self.port = port
        self.sock = None
        self.reader = None
        self._id = 0

    def connect(self):
        # Try ports 27015-27024 (DLL uses fallback if port is taken)
        last_err = None
        for offset in range(10):
            try_port = self.port + offset
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.settimeout(3)
                self.sock.connect((self.host, try_port))
                self.port = try_port
                break
            except (ConnectionRefusedError, OSError) as e:
                last_err = e
                self.sock.close()
                self.sock = None
                continue

        if self.sock is None:
            raise ConnectionRefusedError(f"No MCP server found on ports {self.port}-{self.port+9}: {last_err}")

        self.sock.settimeout(15)
        self.reader = self.sock.makefile("r")
        # MCP handshake
        resp = self._send("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "patchwork-py", "version": "1.0"}
        })
        # Send initialized notification (no response expected)
        self._send_notification("notifications/initialized", {})
        return resp

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def _next_id(self):
        self._id += 1
        return self._id

    def _send(self, method, params=None):
        msg = {
            "jsonrpc": "2.0",
            "method": method,
            "id": self._next_id()
        }
        if params is not None:
            msg["params"] = params
        raw = json.dumps(msg) + "\n"
        self.sock.sendall(raw.encode())
        line = self.reader.readline()
        if not line:
            raise ConnectionError("Server closed connection")
        return json.loads(line)

    def _send_notification(self, method, params=None):
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        raw = json.dumps(msg) + "\n"
        self.sock.sendall(raw.encode())

    def call_tool(self, name, arguments=None):
        resp = self._send("tools/call", {
            "name": name,
            "arguments": arguments or {}
        })
        return resp

    def list_tools(self):
        return self._send("tools/list")

    # ── Convenience methods ──

    def execute_lua(self, code):
        return self.call_tool("execute_lua", {"code": code})

    def execute_file(self, path):
        return self.call_tool("execute_file", {"path": path})

    def get_console(self, count=50):
        return self.call_tool("get_console", {"count": count})

    def clear_console(self):
        return self.call_tool("clear_console")

    def list_scripts(self):
        return self.call_tool("list_scripts")

    def read_memory(self, address, size):
        return self.call_tool("read_memory", {"address": address, "size": size})

    def write_memory(self, address, bytes_list):
        return self.call_tool("write_memory", {"address": address, "bytes": bytes_list})

    def get_status(self):
        return self.call_tool("get_status")

    def ping(self):
        return self._send("ping")


def extract_text(resp):
    """Extract the text content from an MCP tool response."""
    try:
        content = resp.get("result", {}).get("content", [])
        return "\n".join(item["text"] for item in content if item.get("type") == "text")
    except (KeyError, TypeError):
        return json.dumps(resp, indent=2)


def print_resp(label, resp):
    text = extract_text(resp)
    is_error = resp.get("result", {}).get("isError", False)
    prefix = "ERROR" if is_error else "OK"
    print(f"\n[{prefix}] {label}:")
    print(text)


def run_tests(client):
    """Run a comprehensive test of all MCP tools."""
    print("=" * 60)
    print("  PatchWork MCP Server — Test Suite")
    print("=" * 60)

    # 1. Ping
    print("\n--- ping ---")
    resp = client.ping()
    print(f"Ping response: {json.dumps(resp)}")

    # 2. List tools
    print("\n--- tools/list ---")
    resp = client.list_tools()
    tools = resp.get("result", {}).get("tools", [])
    print(f"Available tools ({len(tools)}):")
    for t in tools:
        print(f"  • {t['name']}: {t['description'][:60]}...")

    # 3. Get status
    print("\n--- get_status ---")
    print_resp("Status", client.get_status())

    # 4. Execute Lua — simple print
    print("\n--- execute_lua (print) ---")
    print_resp("print test", client.execute_lua('print("Hello from MCP!")'))

    # 5. Execute Lua — math
    print("\n--- execute_lua (math) ---")
    print_resp("math test", client.execute_lua('print("2 + 2 = " .. tostring(2 + 2))'))

    # 6. Execute Lua — error handling
    print("\n--- execute_lua (error) ---")
    print_resp("error test", client.execute_lua('error("intentional error")'))

    # 7. Execute Lua — multi-line
    print("\n--- execute_lua (multi-line) ---")
    code = """
local t = {}
for i = 1, 5 do
    t[i] = i * i
end
print("Squares: " .. table.concat(t, ", "))
"""
    print_resp("multi-line test", client.execute_lua(code))

    # 8. Get console
    print("\n--- get_console ---")
    print_resp("Console (last 10 lines)", client.get_console(10))

    # 9. List scripts
    print("\n--- list_scripts ---")
    print_resp("Scripts", client.list_scripts())

    # 10. Read memory (read from a safe address — the DLL's own base)
    print("\n--- read_memory (first 32 bytes of ntdll) ---")
    # Read from ntdll base which is always mapped
    import ctypes
    ntdll = ctypes.windll.ntdll
    ntdll_base = ctypes.cast(ntdll._handle, ctypes.c_void_p).value
    addr = f"0x{ntdll_base:X}"
    print_resp(f"read_memory({addr}, 32)", client.read_memory(addr, 32))

    # 11. Clear console
    print("\n--- clear_console ---")
    print_resp("Clear", client.clear_console())

    print("\n" + "=" * 60)
    print("  All tests complete!")
    print("=" * 60)


def interactive(client):
    """Interactive Lua REPL over MCP."""
    print("\nPatchWork MCP — Interactive Lua Console")
    print("Type Lua code to execute. Commands: /status /console /scripts /clear /quit\n")

    while True:
        try:
            line = input("lua> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not line:
            continue
        if line == "/quit":
            break
        elif line == "/status":
            print_resp("Status", client.get_status())
        elif line == "/console":
            print_resp("Console", client.get_console())
        elif line == "/scripts":
            print_resp("Scripts", client.list_scripts())
        elif line == "/clear":
            print_resp("Clear", client.clear_console())
        elif line == "/tools":
            resp = client.list_tools()
            tools = resp.get("result", {}).get("tools", [])
            for t in tools:
                print(f"  {t['name']}")
        else:
            print_resp("Result", client.execute_lua(line))


def main():
    args = sys.argv[1:]

    client = PatchWorkMCP()
    try:
        print(f"Scanning for PatchWork MCP on ports {PORT}-{PORT+9}...")
        init_resp = client.connect()
        server_info = init_resp.get("result", {}).get("serverInfo", {})
        print(f"Connected to {server_info.get('name', '?')} v{server_info.get('version', '?')} on port {client.port}")
    except ConnectionRefusedError:
        print(f"ERROR: Cannot connect to {HOST}:{PORT}")
        print("Make sure PatchWork is injected and the overlay is loaded.")
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)

    try:
        if not args:
            interactive(client)
        elif args[0] == "test":
            run_tests(client)
        elif args[0] == "status":
            print_resp("Status", client.get_status())
        elif args[0] == "exec" and len(args) > 1:
            print_resp("Exec", client.execute_lua(" ".join(args[1:])))
        elif args[0] == "run" and len(args) > 1:
            print_resp("Run", client.execute_file(args[1]))
        elif args[0] == "console":
            count = int(args[1]) if len(args) > 1 else 50
            print_resp("Console", client.get_console(count))
        elif args[0] == "clear":
            print_resp("Clear", client.clear_console())
        elif args[0] == "scripts":
            print_resp("Scripts", client.list_scripts())
        elif args[0] == "read" and len(args) > 2:
            print_resp("Read", client.read_memory(args[1], int(args[2])))
        elif args[0] == "write" and len(args) > 2:
            bytes_list = [int(b, 16) if b.startswith("0x") else int(b) for b in args[2:]]
            print_resp("Write", client.write_memory(args[1], bytes_list))
        elif args[0] == "tools":
            resp = client.list_tools()
            tools = resp.get("result", {}).get("tools", [])
            for t in tools:
                schema = t.get("inputSchema", {})
                props = list(schema.get("properties", {}).keys())
                print(f"  {t['name']}({', '.join(props)})")
                print(f"    {t['description']}")
        else:
            print(__doc__)
    finally:
        client.close()


if __name__ == "__main__":
    main()
