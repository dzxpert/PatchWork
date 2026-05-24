
#include "MemoryFunctions.h"

#include "CodeFunctions.h"

template <typename T>
static bool SafeRead(uintptr_t address, T& value) {
	__try {
		value = *(const T*)address;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

template <typename T>
static bool SafeWrite(uintptr_t address, T value) {
	__try {
		*(T*)address = value;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool SafeCopy(void* dst, const void* src, size_t size) {
	__try {
		memcpy(dst, src, size);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool SafeMemset(void* dst, int val, size_t size) {
	__try {
		memset(dst, val, size);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool SafeReadStringNullTerminated(uintptr_t address, std::string& result, size_t maxLen = 65536) {
	result.clear();
	char c = 0;
	for (size_t i = 0; i < maxLen; i++) {
		if (!SafeRead(address + i, c)) {
			return false;
		}
		if (c == '\0') {
			return true;
		}
		result.push_back(c);
	}
	return true;
}

int luaReadByte(lua_State* L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "expected exactly 1 argument");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	BYTE value;
	if (!SafeRead(address, value)) {
		return luaL_error(L, "Access violation reading address 0x%p", (void*)address);
	}
	lua_pushinteger(L, value);
	return 1;
}

int luaReadSmallInteger(lua_State* L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "expected exactly 1 argument");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	SHORT value;
	if (!SafeRead(address, value)) {
		return luaL_error(L, "Access violation reading address 0x%p", (void*)address);
	}
	lua_pushinteger(L, value);
	return 1;
}

int luaReadInteger(lua_State* L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "expected exactly 1 argument");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	int value;
	if (!SafeRead(address, value)) {
		return luaL_error(L, "Access violation reading address 0x%p", (void*)address);
	}
	lua_pushinteger(L, value);
	return 1;
}

int luaReadString(lua_State* L) {
	uintptr_t address = 0;
	size_t length = 0;
	bool wide = false;
	if (lua_gettop(L) == 0) {
		return luaL_error(L, "too few arguments passed to readString");
	}
	address = lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}

	if (lua_gettop(L) == 2) {
		if (!lua_isnil(L, 2)) {
			length = lua_tointeger(L, 2);
		}
	}
	if (lua_gettop(L) == 3) {
		wide = lua_tointeger(L, 3) == 1;
	}

	if (wide) {
		return luaL_error(L, "sorry, wide string is not supported yet.");
	}

	if (length > 0) {
		std::string buffer(length, '\0');
		if (!SafeCopy(&buffer[0], (const void*)address, length)) {
			return luaL_error(L, "Access violation reading address 0x%p", (void*)address);
		}
		lua_pushlstring(L, buffer.data(), length);
	}
	else {
		std::string result;
		if (!SafeReadStringNullTerminated(address, result)) {
			return luaL_error(L, "Access violation reading address 0x%p", (void*)address);
		}
		lua_pushstring(L, result.c_str());
	}

	return 1;
}

int luaReadBytes(lua_State* L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expected exactly 2 arguments");
	}

	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}

	int size = lua_tointeger(L, 2);

	lua_createtable(L, size, 0);

	for (int i = 0; i < size; i++) {
		unsigned char value;
		if (!SafeRead(address + i, value)) {
			return luaL_error(L, "Access violation reading address 0x%p", (void*)(address + i));
		}
		lua_pushinteger(L, (lua_Integer)i + 1);
		lua_pushinteger(L, value);
		lua_settable(L, -3);  /* 3rd element from the stack top */
	}

	return 1;
}

int luaWriteString(lua_State* L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expected exactly 2 arguments");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}

	size_t size = 0;
	const char* value = lua_tolstring(L, 2, &size);

#ifdef _DEBUG
	if (!canWrite(address, size)) {
		return luaL_error(L, "cannot write %d string to location: 0x%X", 1, address);
	}
#endif

	if (!SafeCopy((void*)address, value, size)) {
		return luaL_error(L, "Access violation writing address 0x%p", (void*)address);
	}

	return 0;
}

int luaWriteByte(lua_State* L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expected exactly 2 arguments");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}

	BYTE value = lua_tointeger(L, 2);

#ifdef _DEBUG
	if (!canWrite(address, 1)) {
		return luaL_error(L, "cannot write 1 bytes to location: 0x%X", address);
	}
#endif

	if (!SafeWrite<BYTE>(address, value)) {
		return luaL_error(L, "Access violation writing address 0x%p", (void*)address);
	}
	return 0;
}

int luaWriteSmallInteger(lua_State* L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expected exactly 2 arguments");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}

	SHORT value = lua_tointeger(L, 2);

#ifdef _DEBUG
	if (!canWrite(address, 2)) {
		return luaL_error(L, "cannot write 2 bytes to location: 0x%X", address);
	}
#endif

	if (!SafeWrite<SHORT>(address, value)) {
		return luaL_error(L, "Access violation writing address 0x%p", (void*)address);
	}
	return 0;
}

int luaWriteInteger(lua_State* L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expected exactly 2 arguments");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}

	int value = lua_tointeger(L, 2);

#ifdef _DEBUG
	if (!canWrite(address, 4)) {
		return luaL_error(L, "cannot write 4 bytes to location: 0x%X", address);
	}
#endif

	if (!SafeWrite<int>(address, value)) {
		return luaL_error(L, "Access violation writing address 0x%p", (void*)address);
	}
	return 0;
}

int luaWriteBytes(lua_State* L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expected exactly 2 arguments");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}

	if (!lua_istable(L, 2)) {
		return luaL_error(L, "the second argument should be a table");
	}

#ifdef _DEBUG
	int length = lua_rawlen(L, 2);
	if (!canWrite(address, length)) {
		return luaL_error(L, "cannot write %d bytes to location: 0x%X", length, address);
	}
#endif

	// Makes use the of the table at -1 (2)
	std::stringstream bytes;
	int returnCode = convertTableToByteStream(L, &bytes);

	if (returnCode == -1) {
		return luaL_error(L, "The return value table must have integer values");
	}
	else if (returnCode == -2) {
		return luaL_error(L, "The values must all be positive");
	}

	bytes.seekg(0, bytes.end);
	int size = bytes.tellg();
	bytes.seekg(0, bytes.beg);

	if (!SafeCopy((void*)address, bytes.str().data(), size)) {
		return luaL_error(L, "Access violation writing address 0x%p", (void*)address);
	}

	return 0;
}

int luaMemCpy(lua_State* L) {

	if (lua_gettop(L) != 3) {
		return luaL_error(L, "expected exactly 3 arguments");
	}

	uintptr_t dst = (uintptr_t)lua_tointeger(L, 1);
	if (dst == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	
	uintptr_t src = (uintptr_t)lua_tointeger(L, 2);
	if (src == 0) {
		return luaL_error(L, "argument 2 must be a valid address");
	}

	int size = lua_tointeger(L, 3);
	if (size == 0) {
		return luaL_error(L, "argument 3 must be a valid size higher than 0");
	}

#ifdef _DEBUG
	if (!canWrite(dst, size)) {
		return luaL_error(L, "cannot write %d bytes to location: 0x%X", size, dst);
	}
#endif

	if (!SafeCopy((void*)dst, (void*)src, size)) {
		return luaL_error(L, "Access violation copying %d bytes from 0x%p to 0x%p", size, (void*)src, (void*)dst);
	}

	return 0;
}


int luaMemSet(lua_State* L) {

	if (lua_gettop(L) != 3) {
		return luaL_error(L, "expected exactly 3 arguments");
	}

	uintptr_t dst = (uintptr_t)lua_tointeger(L, 1);
	if (dst == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}

	if (lua_type(L, 2) != LUA_TNUMBER) {
		return luaL_error(L, "argument 2 must be a valid integer");
	}
	DWORD val = lua_tointeger(L, 2);


	int size = lua_tointeger(L, 3);
	if (size == 0) {
		return luaL_error(L, "argument 3 must be a valid size higher than 0");
	}

#ifdef _DEBUG
	if (!canWrite(dst, size)) {
		return luaL_error(L, "cannot write %d bytes to location: 0x%X", size, dst);
	}
#endif

	if (!SafeMemset((void*)dst, val, size)) {
		return luaL_error(L, "Access violation in memset at address 0x%p", (void*)dst);
	}

	return 0;
}


std::set<std::string> stringSet;

int registerString(lua_State* L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "Wrong number of arguments passed");
	}

	std::string target = lua_tostring(L, 1);
	std::pair<std::set<std::string>::iterator, bool> p = stringSet.insert(target);

	std::set<std::string>::iterator it = p.first;
	lua_pushinteger(L, (lua_Integer)(uintptr_t)p.first->c_str());

	return 1;
}



int luaAllocate(lua_State* L) {
	if (lua_gettop(L) != 1 && lua_gettop(L) != 2) {
		return luaL_error(L, "Expected one or two arguments");
	}

	void* memory;

	int size = lua_tonumber(L, 1);
	if (lua_gettop(L) == 2 && lua_toboolean(L, 2)) {
		memory = calloc(size, sizeof(BYTE));
	}
	else {
		memory = malloc(size);
	}

	lua_pushinteger(L, (DWORD_PTR)memory);

	return 1;
}

int luaDeallocate(lua_State* L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "Expected one argument");
	}

	uintptr_t addr = (uintptr_t)luaL_checkinteger(L, 1);
	if (addr == 0) {
		return luaL_error(L, "Address is 0");
	}

	void* memory = (void* )((DWORD_PTR) addr);
	free(memory);

	return 0;
}

int luaReadQword(lua_State* L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "expected exactly 1 argument");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	unsigned long long value;
	if (!SafeRead(address, value)) {
		return luaL_error(L, "Access violation reading address 0x%p", (void*)address);
	}
	lua_pushinteger(L, (lua_Integer)value);
	return 1;
}

int luaWriteQword(lua_State* L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expected exactly 2 arguments");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	unsigned long long value = lua_tointeger(L, 2);
	if (!SafeWrite<unsigned long long>(address, value)) {
		return luaL_error(L, "Access violation writing address 0x%p", (void*)address);
	}
	return 0;
}

int luaReadFloat(lua_State* L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "expected exactly 1 argument");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	float value;
	if (!SafeRead(address, value)) {
		return luaL_error(L, "Access violation reading address 0x%p", (void*)address);
	}
	lua_pushnumber(L, (lua_Number)value);
	return 1;
}

int luaWriteFloat(lua_State* L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expected exactly 2 arguments");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	float value = (float)lua_tonumber(L, 2);
	if (!SafeWrite<float>(address, value)) {
		return luaL_error(L, "Access violation writing address 0x%p", (void*)address);
	}
	return 0;
}

int luaReadDouble(lua_State* L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "expected exactly 1 argument");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	double value;
	if (!SafeRead(address, value)) {
		return luaL_error(L, "Access violation reading address 0x%p", (void*)address);
	}
	lua_pushnumber(L, (lua_Number)value);
	return 1;
}

int luaWriteDouble(lua_State* L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "expected exactly 2 arguments");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		return luaL_error(L, "argument 1 must be a valid address");
	}
	double value = (double)lua_tonumber(L, 2);
	if (!SafeWrite<double>(address, value)) {
		return luaL_error(L, "Access violation writing address 0x%p", (void*)address);
	}
	return 0;
}

int luaIsValidAddress(lua_State* L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "expected exactly 1 argument");
	}
	uintptr_t address = (uintptr_t)lua_tointeger(L, 1);
	if (address == 0) {
		lua_pushboolean(L, false);
		return 1;
	}
	MEMORY_BASIC_INFORMATION mbi = { 0 };
	if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) != 0) {
		bool readable = (mbi.State == MEM_COMMIT) && 
		                ((mbi.Protect & PAGE_NOACCESS) == 0) &&
		                ((mbi.Protect & PAGE_GUARD) == 0);
		lua_pushboolean(L, readable);
	} else {
		lua_pushboolean(L, false);
	}
	return 1;
}

