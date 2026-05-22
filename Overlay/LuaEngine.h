#pragma once

#include <string>

struct lua_State;

namespace LuaEngine {
    void Init();
    void Shutdown();
    void Reset();

    struct ExecuteResult {
        bool success;
        std::string output;
        std::string error;
    };

    ExecuteResult ExecuteString(const std::string& code);
    ExecuteResult ExecuteFile(const std::string& path);
    lua_State* GetState();
}
