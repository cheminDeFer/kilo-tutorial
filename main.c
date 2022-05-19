#include <stdio.h>
#include "lua.h"
#include <lualib.h>
#include <lauxlib.h>
int main(int argc, char* argv[])
{
    printf("hello world\n");
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dofile(L, "script.lua") == LUA_OK) {
	lua_pop(L, lua_gettop(L));
    } else {
	fprintf(stderr, "error in script\n");
	lua_close(L);
	return 1;
    }
    lua_getglobal(L, "message");
    if (lua_isstring(L, -1)) {
	const char * message = lua_tostring(L, -1);
	lua_pop(L, 1);
	printf("Message from script : %s\n", message);
    }


    lua_getglobal(L, "x");
    if (lua_isnumber(L, -1)) {
	const double myx = lua_tonumber(L,-1);
	lua_pop(L,1);
	printf("Value from script : %g\n", myx);
    }

    lua_close(L);
    return 0;
}
