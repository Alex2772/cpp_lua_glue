//
// Created by alex2 on 02.05.2021.
//

#pragma once

#include <cstdlib>
#include <thread>
#include <cassert>

extern "C" {
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}

namespace clg {

    inline void checkThread() {
        static std::thread::id threadId = std::this_thread::get_id();
        assert(threadId == std::this_thread::get_id());
    }

    static bool isInExitHandler() {
        // Lua seems feel bad on exit. Avoid unnecessary calls to Lua as the process is exiting anyway.
        static bool v = false;
        if (static bool once = true; once) {
            once = false;
            std::atexit([] {
                v = true;
                });
        }
        return v;
    }

}

// silence overflow warnings
#undef lua_pop
#define lua_pop(L,n) lua_settop(L, static_cast<int>(-(n)-1)) // silence overflow warning

#undef luaL_newlibtable
#define luaL_newlibtable(L,l) lua_createtable(L, 0, int(sizeof(l)/sizeof((l)[0]) - 1))