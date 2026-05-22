#include "LuaEngine.h"
#include "Console.h"
#include "../PatchWork.h"

#include "lua.hpp"
#include <sstream>

static lua_State* s_luaState = nullptr;

// Custom print function that redirects output to the Console
static int lua_print_override(lua_State* L)
{
    int nargs = lua_gettop(L);
    std::string result;
    for (int i = 1; i <= nargs; i++)
    {
        if (i > 1) result += "\t";
        const char* s = luaL_tolstring(L, i, nullptr);
        if (s) result += s;
        lua_pop(L, 1); // pop the string from luaL_tolstring
    }
    Console::AddLine(result);
    return 0;
}

namespace LuaEngine
{
    void Init()
    {
        if (s_luaState) return;

        // Create new Lua state with standard libraries
        s_luaState = luaL_newstate();
        luaL_openlibs(s_luaState);

        // Register all RPS functions globally
        RPS_setLuaState(s_luaState);
        RPS_initializeLuaAPI(s_luaState, "global");

        // Override print to redirect to our Console
        lua_pushcfunction(s_luaState, lua_print_override);
        lua_setglobal(s_luaState, "print");

        // Set package.path to include PatchWork scripts directory
        RPS_setupPackagePath(s_luaState, "C:\\PatchWork\\scripts\\?.lua");

        Console::AddLine("Lua engine initialized", ImVec4(0.0f, 0.831f, 1.0f, 1.0f));
    }

    void Shutdown()
    {
        if (s_luaState)
        {
            lua_close(s_luaState);
            s_luaState = nullptr;
        }
    }

    void Reset()
    {
        Console::AddLine("Resetting Lua state...", ImVec4(1.0f, 0.843f, 0.0f, 1.0f));
        Shutdown();
        Init();
        Console::AddLine("Lua state reset complete", ImVec4(0.224f, 1.0f, 0.078f, 1.0f));
    }

    ExecuteResult ExecuteString(const std::string& code)
    {
        ExecuteResult result;
        result.success = false;

        if (!s_luaState)
        {
            result.error = "Lua state not initialized";
            return result;
        }

        Console::AddLine("> " + code, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

        int status = luaL_dostring(s_luaState, code.c_str());
        if (status == LUA_OK)
        {
            result.success = true;
            // Check if there's a return value on the stack
            if (lua_gettop(s_luaState) > 0)
            {
                const char* s = luaL_tolstring(s_luaState, -1, nullptr);
                if (s)
                {
                    result.output = s;
                    Console::AddLine(result.output);
                }
                lua_pop(s_luaState, 2); // pop result + tolstring
            }
        }
        else
        {
            const char* err = lua_tostring(s_luaState, -1);
            result.error = err ? err : "Unknown error";
            Console::AddLine(result.error, ImVec4(1.0f, 0.267f, 0.267f, 1.0f));
            lua_pop(s_luaState, 1);
        }

        return result;
    }

    ExecuteResult ExecuteFile(const std::string& path)
    {
        ExecuteResult result;
        result.success = false;

        if (!s_luaState)
        {
            result.error = "Lua state not initialized";
            return result;
        }

        Console::AddLine("Running file: " + path, ImVec4(0.0f, 0.831f, 1.0f, 1.0f));

        int status = luaL_dofile(s_luaState, path.c_str());
        if (status == LUA_OK)
        {
            result.success = true;
            Console::AddLine("File executed successfully", ImVec4(0.224f, 1.0f, 0.078f, 1.0f));
        }
        else
        {
            const char* err = lua_tostring(s_luaState, -1);
            result.error = err ? err : "Unknown error";
            Console::AddLine(result.error, ImVec4(1.0f, 0.267f, 0.267f, 1.0f));
            lua_pop(s_luaState, 1);
        }

        return result;
    }

    lua_State* GetState()
    {
        return s_luaState;
    }
}
