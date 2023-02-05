//
// Created by Alex2772 on 11/3/2021.
//

#pragma once

#if defined(__linux) || defined(__APPLE__)
#include <cxxabi.h>
#endif

#include <algorithm>
#include <cctype>
#include <string>
#include <cassert>

namespace clg {
    template<class T>
    std::string class_name() {
        std::string s = typeid(T).name();
#if defined(__linux) || defined(__APPLE__)
        int status;
        auto c = abi::__cxa_demangle(s.c_str(), 0, 0, &status);
        s = c;
        delete[] c;
#endif

        auto it = std::find_if(s.rbegin(),  s.rend(), [](char c) {
            return !std::isalnum(c);
        });

        return {it.base(), s.end()};
    }


    /**
     * @brief проверяет, что стек луа не поменялся в скоупе RAII
     */
    struct stack_integrity_check {
    public:
        stack_integrity_check(lua_State* lua, int expectedDifference = 0): mLua(lua), mExpectedDifference(expectedDifference) {
            mStackBegin = lua_gettop(mLua);
        }

        ~stack_integrity_check() {
            auto stackEnd = lua_gettop(mLua);
            const auto actualDifference = stackEnd - mStackBegin;
            assert(actualDifference == mExpectedDifference);
        }

    private:
        lua_State* mLua;
        int mStackBegin;
        int mExpectedDifference;
    };


    static void print_stack(lua_State* L) {
        int top=lua_gettop(L);
        for (int i=1; i <= top; i++) {
            printf("%d\t%d\t%s\t", i, i - top - 1, luaL_typename(L,i));
            switch (lua_type(L, i)) {
                case LUA_TNUMBER:
                    printf("%g\n",lua_tonumber(L,i));
                    break;
                case LUA_TSTRING:
                    printf("%s\n",lua_tostring(L,i));
                    break;
                case LUA_TBOOLEAN:
                    printf("%s\n", (lua_toboolean(L, i) ? "true" : "false"));
                    break;
                case LUA_TNIL:
                    printf("%s\n", "nil");
                    break;
                default:
                    printf("%p\n",lua_topointer(L,i));
                    break;
            }
        }
    }
}