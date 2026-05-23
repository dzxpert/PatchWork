#include "MCPServer.h"
#include "LuaEngine.h"
#include "Console.h"

#include <nlohmann/json.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <future>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════
//  Thread-safe command queue: MCP thread enqueues, render thread executes
// ═══════════════════════════════════════════════════════════════════

struct LuaCommand {
    std::string              code;
    std::promise<json>       promise;
};

static std::mutex              s_cmdMutex;
static std::queue<LuaCommand>  s_cmdQueue;

// ═══════════════════════════════════════════════════════════════════
//  Server state
// ═══════════════════════════════════════════════════════════════════

static std::atomic<bool> s_running{false};
static std::thread       s_serverThread;
static SOCKET            s_listenSocket = INVALID_SOCKET;

// ═══════════════════════════════════════════════════════════════════
//  Tool definitions (MCP schema)
// ═══════════════════════════════════════════════════════════════════

static json BuildToolList()
{
    return json::array({
        {
            {"name", "execute_lua"},
            {"description", "Execute a Lua script string inside the target process. Returns stdout/output and errors."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"code", {{"type", "string"}, {"description", "Lua source code to execute"}}}
                }},
                {"required", json::array({"code"})}
            }}
        },
        {
            {"name", "execute_file"},
            {"description", "Execute a Lua script file from disk inside the target process."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Absolute path to .lua file"}}}
                }},
                {"required", json::array({"path"})}
            }}
        },
        {
            {"name", "get_console"},
            {"description", "Get recent console output lines from PatchWork."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"count", {{"type", "integer"}, {"description", "Max lines to return (default 50)"}}}
                }}
            }}
        },
        {
            {"name", "clear_console"},
            {"description", "Clear the PatchWork console."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        },
        {
            {"name", "list_scripts"},
            {"description", "List all .lua script files in C:\\PatchWork\\scripts\\."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        },
        {
            {"name", "read_memory"},
            {"description", "Read bytes from a memory address in the target process."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"address", {{"type", "string"}, {"description", "Hex address (e.g. '0x7FF612340000')"}}},
                    {"size",    {{"type", "integer"}, {"description", "Number of bytes to read (max 4096)"}}}
                }},
                {"required", json::array({"address", "size"})}
            }}
        },
        {
            {"name", "write_memory"},
            {"description", "Write bytes to a memory address in the target process."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"address", {{"type", "string"}, {"description", "Hex address (e.g. '0x7FF612340000')"}}},
                    {"bytes",   {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Array of byte values (0-255)"}}}
                }},
                {"required", json::array({"address", "bytes"})}
            }}
        },
        {
            {"name", "get_status"},
            {"description", "Get PatchWork status: whether overlay is visible, Lua state info, etc."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        }
    });
}

// ═══════════════════════════════════════════════════════════════════
//  SEH-protected memory access (must be in separate functions
//  because MSVC forbids __try in functions with C++ unwinding)
// ═══════════════════════════════════════════════════════════════════

// Returns true on success, false on access violation
static bool SafeMemRead(void* dst, const void* src, size_t size)
{
    __try {
        memcpy(dst, src, size);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeMemWrite(void* dst, const void* src, size_t size)
{
    __try {
        memcpy(dst, src, size);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Tool handlers (called from the appropriate thread)
// ═══════════════════════════════════════════════════════════════════

// Queue a Lua execution and block until the render thread completes it.
static json QueueLuaExecution(const std::string& code)
{
    std::promise<json> prom;
    auto fut = prom.get_future();
    {
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        s_cmdQueue.push({code, std::move(prom)});
    }
    // Wait for render thread to execute (timeout 10s)
    if (fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready)
        return fut.get();

    return json{{"content", json::array({{{"type", "text"}, {"text", "Execution timed out (10s)"}}}), {"isError", true}}};
}

static json HandleToolCall(const std::string& toolName, const json& args)
{
    // ── execute_lua ──
    if (toolName == "execute_lua")
    {
        std::string code = args.value("code", "");
        if (code.empty())
            return {{"content", json::array({{{"type", "text"}, {"text", "Error: 'code' argument is required"}}})}, {"isError", true}};
        return QueueLuaExecution(code);
    }

    // ── execute_file ──
    if (toolName == "execute_file")
    {
        std::string path = args.value("path", "");
        if (path.empty())
            return {{"content", json::array({{{"type", "text"}, {"text", "Error: 'path' argument is required"}}})}, {"isError", true}};

        std::ifstream f(path);
        if (!f.is_open())
            return {{"content", json::array({{{"type", "text"}, {"text", "Error: cannot open file: " + path}}})}, {"isError", true}};

        std::string code((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return QueueLuaExecution(code);
    }

    // ── get_console ── (thread-safe read)
    if (toolName == "get_console")
    {
        int count = args.value("count", 50);
        const auto& lines = Console::GetLines();
        int start = (int)lines.size() - count;
        if (start < 0) start = 0;

        std::string text;
        for (int i = start; i < (int)lines.size(); i++)
            text += lines[i].text + "\n";
        return {{"content", json::array({{{"type", "text"}, {"text", text}}})}};
    }

    // ── clear_console ──
    if (toolName == "clear_console")
    {
        Console::Clear();
        return {{"content", json::array({{{"type", "text"}, {"text", "Console cleared"}}})}};
    }

    // ── list_scripts ──
    if (toolName == "list_scripts")
    {
        std::string listing;
        try {
            for (const auto& entry : std::filesystem::directory_iterator("C:\\PatchWork\\scripts"))
            {
                if (entry.path().extension() == ".lua")
                    listing += entry.path().filename().string() + "\n";
            }
        } catch (...) {
            listing = "(directory not found or empty)";
        }
        if (listing.empty()) listing = "(no scripts found)";
        return {{"content", json::array({{{"type", "text"}, {"text", listing}}})}};
    }

    // ── read_memory ──
    if (toolName == "read_memory")
    {
        std::string addrStr = args.value("address", "");
        int size = args.value("size", 0);
        if (addrStr.empty() || size <= 0)
            return {{"content", json::array({{{"type", "text"}, {"text", "Error: 'address' and 'size' required"}}})}, {"isError", true}};
        if (size > 4096) size = 4096;

        uintptr_t addr = 0;
        try { addr = std::stoull(addrStr, nullptr, 16); } catch (...) {
            return {{"content", json::array({{{"type", "text"}, {"text", "Error: invalid hex address"}}})}, {"isError", true}};
        }

        std::vector<uint8_t> buf(size);
        if (!SafeMemRead(buf.data(), (void*)addr, size))
            return {{"content", json::array({{{"type", "text"}, {"text", "Error: access violation reading address"}}})}, {"isError", true}};

        // Format as hex dump
        std::string hex;
        char tmp[8];
        for (int i = 0; i < size; i++) {
            sprintf_s(tmp, "%02X ", buf[i]);
            hex += tmp;
            if ((i + 1) % 16 == 0) hex += "\n";
        }
        return {{"content", json::array({{{"type", "text"}, {"text", hex}}})}};
    }

    // ── write_memory ──
    if (toolName == "write_memory")
    {
        std::string addrStr = args.value("address", "");
        if (addrStr.empty() || !args.contains("bytes"))
            return {{"content", json::array({{{"type", "text"}, {"text", "Error: 'address' and 'bytes' required"}}})}, {"isError", true}};

        uintptr_t addr = 0;
        try { addr = std::stoull(addrStr, nullptr, 16); } catch (...) {
            return {{"content", json::array({{{"type", "text"}, {"text", "Error: invalid hex address"}}})}, {"isError", true}};
        }

        auto& bytesArr = args["bytes"];
        std::vector<uint8_t> data;
        for (auto& b : bytesArr)
            data.push_back((uint8_t)(b.get<int>() & 0xFF));

        DWORD oldProt;
        VirtualProtect((void*)addr, data.size(), PAGE_EXECUTE_READWRITE, &oldProt);
        bool ok = SafeMemWrite((void*)addr, data.data(), data.size());
        VirtualProtect((void*)addr, data.size(), oldProt, &oldProt);

        if (!ok)
            return {{"content", json::array({{{"type", "text"}, {"text", "Error: access violation writing address"}}})}, {"isError", true}};

        char msg[128];
        sprintf_s(msg, "Wrote %d bytes to 0x%llX", (int)data.size(), (unsigned long long)addr);
        return {{"content", json::array({{{"type", "text"}, {"text", std::string(msg)}}})}};
    }

    // ── get_status ──
    if (toolName == "get_status")
    {
        extern bool g_ShowOverlay;
        json status = {
            {"overlay_visible", g_ShowOverlay},
            {"lua_state", LuaEngine::GetState() != nullptr ? "active" : "null"},
            {"console_lines", (int)Console::GetLines().size()},
            {"mcp_server", "running"}
        };
        return {{"content", json::array({{{"type", "text"}, {"text", status.dump(2)}}})}};
    }

    return {{"content", json::array({{{"type", "text"}, {"text", "Unknown tool: " + toolName}}})}, {"isError", true}};
}

// ═══════════════════════════════════════════════════════════════════
//  JSON-RPC 2.0 / MCP message router
// ═══════════════════════════════════════════════════════════════════

static json HandleMessage(const json& msg)
{
    std::string method = msg.value("method", "");
    auto id = msg.contains("id") ? msg["id"] : json(nullptr);
    bool isNotification = id.is_null();

    // ── initialize ──
    if (method == "initialize")
    {
        json result = {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", {
                {"tools", json::object()}
            }},
            {"serverInfo", {
                {"name", "PatchWork"},
                {"version", "1.0.0"}
            }}
        };
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    }

    // ── notifications/initialized ── (no response needed)
    if (method == "notifications/initialized")
        return json();

    // ── tools/list ──
    if (method == "tools/list")
    {
        json result = {{"tools", BuildToolList()}};
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    }

    // ── tools/call ──
    if (method == "tools/call")
    {
        std::string toolName = msg["params"].value("name", "");
        json args = msg["params"].value("arguments", json::object());
        json toolResult = HandleToolCall(toolName, args);
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", toolResult}};
    }

    // ── ping ──
    if (method == "ping")
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", json::object()}};

    // ── unknown method ──
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32601}, {"message", "Method not found: " + method}}}};
}

// ═══════════════════════════════════════════════════════════════════
//  Single-threaded server using select() — avoids detached thread
//  issues that are common in injected DLL environments.
// ═══════════════════════════════════════════════════════════════════

struct ClientState {
    SOCKET       sock;
    std::string  buffer;
};

static void ProcessClientData(ClientState& client)
{
    OutputDebugStringA("[PW-MCP] ProcessClientData enter\n");
    size_t pos;
    while ((pos = client.buffer.find('\n')) != std::string::npos)
    {
        std::string line = client.buffer.substr(0, pos);
        client.buffer.erase(0, pos + 1);

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty() || line[0] != '{') {
            OutputDebugStringA("[PW-MCP] Skipping non-JSON line\n");
            continue;
        }

        OutputDebugStringA(("[PW-MCP] Parsing: " + line.substr(0, 120) + "\n").c_str());

        try {
            json msg = json::parse(line);
            OutputDebugStringA(("[PW-MCP] Method: " + msg.value("method", "(none)") + "\n").c_str());

            json response = HandleMessage(msg);

            if (response.is_null() || response.empty()) {
                OutputDebugStringA("[PW-MCP] Notification, no response\n");
                continue;
            }

            std::string resp = response.dump() + "\n";
            int sent = send(client.sock, resp.c_str(), (int)resp.size(), 0);
            OutputDebugStringA(("[PW-MCP] Sent " + std::to_string(sent) + " bytes\n").c_str());
        }
        catch (const std::exception& e) {
            OutputDebugStringA(("[PW-MCP] EXCEPTION: " + std::string(e.what()) + "\n").c_str());
            json err = {
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", {{"code", -32700}, {"message", std::string("Parse error: ") + e.what()}}}
            };
            std::string resp = err.dump() + "\n";
            send(client.sock, resp.c_str(), (int)resp.size(), 0);
        }
    }
    OutputDebugStringA("[PW-MCP] ProcessClientData exit\n");
}

static void ServerThread(int port)
{
    OutputDebugStringA("[PW-MCP] ServerThread starting\n");
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        OutputDebugStringA("[PW-MCP] WSAStartup FAILED\n");
        return;
    }
    OutputDebugStringA("[PW-MCP] WSAStartup OK\n");

    s_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listenSocket == INVALID_SOCKET) {
        OutputDebugStringA("[PW-MCP] socket() FAILED\n");
        WSACleanup(); return;
    }

    // Allow port reuse
    int yes = 1;
    setsockopt(s_listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Try up to 10 ports starting from the requested one
    int boundPort = 0;
    for (int attempt = 0; attempt < 10; attempt++)
    {
        int tryPort = port + attempt;
        addr.sin_port = htons((u_short)tryPort);

        if (bind(s_listenSocket, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR)
        {
            boundPort = tryPort;
            OutputDebugStringA(("[PW-MCP] Bound to port " + std::to_string(tryPort) + "\n").c_str());
            break;
        }

        int wsaErr = WSAGetLastError();
        OutputDebugStringA(("[PW-MCP] bind() port " + std::to_string(tryPort)
            + " failed, WSA=" + std::to_string(wsaErr) + ", trying next...\n").c_str());
    }

    if (boundPort == 0)
    {
        OutputDebugStringA("[PW-MCP] All ports failed, giving up\n");
        closesocket(s_listenSocket);
        s_listenSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    listen(s_listenSocket, 4);

    u_long nonBlocking = 1;
    ioctlsocket(s_listenSocket, FIONBIO, &nonBlocking);

    OutputDebugStringA(("[PW-MCP] Listening on port " + std::to_string(boundPort) + " (non-blocking)\n").c_str());
    Console::AddLine("MCP server on localhost:" + std::to_string(boundPort),
                     ImVec4(0.0f, 0.831f, 1.0f, 1.0f));

    std::vector<ClientState> clients;

    while (s_running)
    {
        try
        {
            fd_set readFds;
            FD_ZERO(&readFds);
            FD_SET(s_listenSocket, &readFds);

            for (auto& c : clients)
                FD_SET(c.sock, &readFds);

            timeval tv = {0, 100000}; // 100ms timeout
            int sel = select(0, &readFds, nullptr, nullptr, &tv);
            if (sel < 0) break;
            if (sel == 0) continue;

            // Accept new connections
            if (FD_ISSET(s_listenSocket, &readFds))
            {
                SOCKET newSock = accept(s_listenSocket, nullptr, nullptr);
                if (newSock != INVALID_SOCKET)
                {
                    OutputDebugStringA("[PW-MCP] Client connected\n");
                    u_long nb = 1;
                    ioctlsocket(newSock, FIONBIO, &nb);
                    ClientState cs;
                    cs.sock = newSock;
                    clients.push_back(std::move(cs));
                }
            }

            // Read from existing clients
            for (int i = (int)clients.size() - 1; i >= 0; i--)
            {
                if (!FD_ISSET(clients[i].sock, &readFds))
                    continue;

                char chunk[4096];
                int n = recv(clients[i].sock, chunk, sizeof(chunk) - 1, 0);
                if (n <= 0)
                {
                    int err = WSAGetLastError();
                    OutputDebugStringA(("[PW-MCP] Client recv<=0, n=" + std::to_string(n) + " WSA=" + std::to_string(err) + "\n").c_str());
                    closesocket(clients[i].sock);
                    clients.erase(clients.begin() + i);
                    continue;
                }

                OutputDebugStringA(("[PW-MCP] Recv " + std::to_string(n) + " bytes\n").c_str());
                chunk[n] = '\0';
                clients[i].buffer += chunk;

                try {
                    ProcessClientData(clients[i]);
                } catch (const std::exception& ex) {
                    OutputDebugStringA(("[PW-MCP] ProcessClientData EXCEPTION: " + std::string(ex.what()) + "\n").c_str());
                } catch (...) {
                    OutputDebugStringA("[PW-MCP] ProcessClientData UNKNOWN EXCEPTION\n");
                }
            }
        }
        catch (const std::exception& ex)
        {
            OutputDebugStringA(("[PW-MCP] Loop EXCEPTION: " + std::string(ex.what()) + "\n").c_str());
            Sleep(100);
        }
        catch (...)
        {
            OutputDebugStringA("[PW-MCP] Loop UNKNOWN EXCEPTION\n");
            Sleep(100);
        }
    }

    // Cleanup all clients
    for (auto& c : clients)
        closesocket(c.sock);

    closesocket(s_listenSocket);
    s_listenSocket = INVALID_SOCKET;
    WSACleanup();
}

// ═══════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════

namespace MCPServer
{
    void Start(int port)
    {
        if (s_running) return;
        s_running = true;
        s_serverThread = std::thread(ServerThread, port);
    }

    void Stop()
    {
        s_running = false;

        // Wake up the accept loop by closing the listen socket
        if (s_listenSocket != INVALID_SOCKET)
        {
            closesocket(s_listenSocket);
            s_listenSocket = INVALID_SOCKET;
        }

        if (s_serverThread.joinable())
            s_serverThread.join();
    }

    void Poll()
    {
        // Execute queued Lua commands on the render thread
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        while (!s_cmdQueue.empty())
        {
            auto cmd = std::move(s_cmdQueue.front());
            s_cmdQueue.pop();

            auto result = LuaEngine::ExecuteString(cmd.code);

            json response;
            if (result.success)
            {
                std::string text = result.output.empty() ? "(executed successfully)" : result.output;
                response = {{"content", json::array({{{"type", "text"}, {"text", text}}})}};
            }
            else
            {
                response = {{"content", json::array({{{"type", "text"}, {"text", "Error: " + result.error}}})}, {"isError", true}};
            }

            try { cmd.promise.set_value(response); } catch (...) {}
        }
    }

    bool IsRunning() { return s_running; }
}
