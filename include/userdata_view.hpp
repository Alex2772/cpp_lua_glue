#pragma once

#include "lua.hpp"
#include "converter.hpp"
#include "value.hpp"
#incldue "ref.hpp"


namespace clg {
    class userdata_view : public ref {
    public:
        using ref::ref;

        userdata_view(ref r) : ref(std::move(r)) {
        }

        userdata_helper* asUserdataHelper() {
            clg::stack_integrity_check check;
            auto l = clg::state();
            push_value_to_stack(l);
            auto res = static_cast<userdata_helper*>(lua_touserdata(l, -1));
            lua_pop(l, 1);
            return res;
        }

        clg::ref uservalue() const noexcept {
            clg::stack_integrity_check check;
            auto l = clg::state();
            push_value_to_stack(l);
            lua_getuservalue(l, -1);
            auto res = clg::ref::from_stack(clg::state());
            lua_pop(l, 1);
            return res;
        }
    };

    class clg_userdata_view : public userdata_view {
        using userdata_view::userdata_view;
    };

    template<>
    struct converter<clg::userdata_view> {
        static converter_result<userdata_view> from_lua(lua_State* l, int n) {
            lua_pushvalue(l, n);
            if (!lua_isuserdata(l, n)) {
                return converter_error{"not a userdata"};
            }
            return userdata_view(clg::ref::from_stack(l));
        }
        static int to_lua(lua_State* l, const clg::ref& ref) {
            return clg::push_to_lua(l, ref);
        }
    };

    template<>
    struct converter<clg::clg_userdata_view> {
        static converter_result<userdata_view> from_lua(lua_State* l, int n) {
            lua_pushvalue(l, n);
            if (!is_clg_userdata(l, n)) {
                return converter_error{"not a userdata"};
            }
            return userdata_view(clg::ref::from_stack(l));
        }
        static int to_lua(lua_State* l, const clg::ref& ref) {
            return clg::push_to_lua(l, ref);
        }
    };
}


