//
// Created by alex2 on 02.05.2021.
//

#pragma once

#include <cstdlib>
#include <thread>
#include <cassert>
#include <utility>

extern "C" {
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}

namespace clg {

    inline void check_thread() {
        static std::thread::id threadId = std::this_thread::get_id();
        assert(threadId == std::this_thread::get_id());
    }

    static bool is_in_exit_handler() {
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

    namespace impl {
        /**
         * @brief Storage of lua_State*.
         * @note Do not use this directly, it's not raii safe. use raii_state_updater instead.
         */
        inline lua_State*& state() noexcept {
            static lua_State* state = nullptr;
            return state;
        }

        struct raii_state_updater {
        public:
            raii_state_updater(lua_State* newState) : mOldState(std::exchange(state(), newState)) {}
            ~raii_state_updater() {
                state() = mOldState;
            }

        private:
            lua_State* mOldState;
        };
    }


    /**
     * @brief Returns the latest lua_State which was passed to C++ honoring the coroutine lua_State (if any). Used by
     * clg::ref's destructor.
     */
    inline lua_State* state() noexcept {
        auto s = impl::state();
        assert(s != nullptr); // state is not set (callers of lua::state() are not expecting null)
        return s;
    }
}

// silence overflow warnings
#undef lua_pop
#define lua_pop(L,n) lua_settop(L, static_cast<int>(-(n)-1)) // silence overflow warning

#undef luaL_newlibtable
#define luaL_newlibtable(L,l) lua_createtable(L, 0, int(sizeof(l)/sizeof((l)[0]) - 1))