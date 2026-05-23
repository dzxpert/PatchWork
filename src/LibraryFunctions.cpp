
#include "framework.h"
#include "LibraryFunctions.h"

int luaLoadLibraryA(lua_State* L) {
    LPCSTR dllname = luaL_checkstring(L, 1);
    HMODULE handle = LoadLibraryA(dllname);
    if (handle == NULL) {
        return luaL_error(L, "Could not load library '%s': %I", dllname, GetLastError());
    }
    lua_pushinteger(L, (lua_Integer)(uintptr_t)handle);
    return 1;
}

int luaGetLibraryProcAddressA(lua_State* L) {
    LPCSTR dllname = luaL_checkstring(L, 1);
    HMODULE handle = LoadLibraryA(dllname);
    if (handle == NULL) {
        return luaL_error(L, "Could not load library '%s': %I", dllname, GetLastError());
    }
    LPCSTR funcname = luaL_checkstring(L, 2);
    FARPROC f = GetProcAddress(handle, funcname);
    if (f == NULL) {
        return luaL_error(L, "Could not load function '%s' from library '%s': %I", funcname, dllname, GetLastError());
    }
    lua_pushinteger(L, (lua_Integer)(uintptr_t)f);
    return 1;
}

int luaGetProcAddress(lua_State* L) {
    HMODULE handle = (HMODULE)(uintptr_t)luaL_checkinteger(L, 1);
    if (handle == NULL) {
        return luaL_error(L, "Invalid library handle: %I", handle, GetLastError());
    }
    LPCSTR funcname = luaL_checkstring(L, 2);
    FARPROC f = GetProcAddress(handle, funcname);
    if (f == NULL) {
        return luaL_error(L, "Could not load function '%s' from library '%I': %I", funcname, handle, GetLastError());
    }
    lua_pushinteger(L, (lua_Integer)(uintptr_t)f);
    return 1;
}

int luaGetAddress(lua_State* L) {
    const char* moduleName = nullptr;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        moduleName = luaL_checkstring(L, 1);
    }
    HMODULE handle = GetModuleHandleA(moduleName);
    if (handle == NULL) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, (lua_Integer)(uintptr_t)handle);
    }
    return 1;
}
