#pragma once
// MCP Server — exposes PatchWork tools over TCP (localhost:27015)
// Protocol: line-delimited JSON-RPC 2.0 (MCP compatible)

#include <string>

namespace MCPServer
{
    // Start the TCP server on a background thread.
    // Call once during overlay initialization.
    void Start(int port = 1339);

    // Stop the server and close all connections.
    void Stop();

    // Poll for queued Lua execution requests from MCP clients.
    // MUST be called from the render thread (hkPresent) since lua_State is not thread-safe.
    // Executes any pending commands and sends results back to the waiting client.
    void Poll();

    // Returns true if the server is running.
    bool IsRunning();
}
