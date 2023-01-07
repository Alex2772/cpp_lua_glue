//
// Created by Alex2772 on 11/6/2021.
//

#pragma once

#include "lua.hpp"
#include "converter.hpp"
#include <variant>

namespace clg {
    using value = std::variant<std::nullptr_t, int, float, std::string>;



    template<>
    struct converter<clg::value> {


        template<typename Arg, typename... Args>
        static clg::value converter_impl(lua_State* l, int n) {
            try {
                return converter<Arg>::from_lua(l, n);
            } catch (...) {
                if constexpr(sizeof...(Args) > 0) {
                    return converter_impl<Args...>(l, n);
                } else {
                    return {};
                }
            }
        }

        template<typename... Args>
        static void converter_begin(lua_State* l, int n, std::variant<Args...>& v) {
            v = converter_impl<Args...>(l, n);
        }

        static clg::value from_lua(lua_State* l, int n) {
            clg::value result;
            converter_begin(l, n, result);
            return result;
        }
        /*
        static int to_lua(lua_State* l, std::nullptr_t v) {
            lua_pushnil(l);
            return 1;
        }*/
    };

}