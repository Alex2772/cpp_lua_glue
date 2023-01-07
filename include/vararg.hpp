//
// Created by Alex2772 on 11/8/2021.
//

#pragma once

#include <map>
#include <memory>
#include "converter.hpp"

namespace clg {
    struct vararg: public std::vector<clg::ref> {};

    template<>
    struct converter<vararg> {
        static vararg from_lua(lua_State* l, int n) {
            clg::stack_integrity_check check(l);
            vararg v;
            int count = lua_gettop(l);
            v.reserve(count);
            for (int i = 0; i < count; ++i) {
                lua_pushvalue(l, i + 1);
                v.push_back(clg::ref::from_stack(l));
            }
            return v;
        }
        /*
        static int to_lua(lua_State* l, bool v) {
            return 0;
        }*/
    };
}