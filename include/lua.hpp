//
// Created by alex2 on 02.05.2021.
//

#pragma once

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

}