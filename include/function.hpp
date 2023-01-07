//
// Created by Alex2772 on 10/31/2021.
//

#pragma once

#include "lua.hpp"
#include <utility>
#include <variant>
#include <functional>
#include "converter.hpp"
#include "ref.hpp"
#include "dynamic_result.hpp"

namespace clg {
    class function {
        friend class state_interface;
        template<typename T, typename EnableIf>
        friend struct clg::converter;


    public:
        function(lua_State* lua, ref ref) : mLua(lua), mRef(std::move(ref)) {
            if (mRef == nullptr) {
                return;
            }
            push_function_to_be_called();
            assert((lua_isfunction(mLua, -1)));
            lua_pop(lua, 1);
        }
        function(ref ref) : mLua(ref.lua()), mRef(std::move(ref)) {
        }

        function() = default;
        function(function&& rhs) noexcept: mLua(rhs.mLua), mRef(std::move(rhs.mRef)) {}
        function(const function& rhs): mLua(rhs.mLua), mRef(rhs.mRef) {}

        function& operator=(function&& rhs) noexcept {
            mLua = rhs.mLua;
            mRef = std::move(rhs.mRef);
            return *this;
        }
        function& operator=(std::nullptr_t rhs) noexcept {
            mRef = nullptr;
            return *this;
        }

        template<typename... Args>
        void operator()(Args&& ... args) const {
            if (mRef == nullptr) return;
            push_function_to_be_called();
            if (!lua_isfunction(mLua, -1)) {
                lua_pop(mLua, 1);
                return;
            }
            push(std::forward<Args>(args)...);
            do_call(sizeof...(args), 0);
        }

        template<typename Return, typename... Args>
        Return call(Args&& ... args) const {
            stack_integrity_check stack(mLua);
            push_function_to_be_called();

            push(std::forward<Args>(args)...);

            if constexpr (std::is_same_v < Return, clg::dynamic_result >) {
                do_call(sizeof...(args), LUA_MULTRET);
                return get_from_lua<Return>(mLua);
            } else if constexpr (std::is_same_v < Return, void >) {
                do_call(sizeof...(args), 0);
            } else {
                do_call(sizeof...(args), 1);
                if (lua_gettop(mLua) != 1) {
                    throw clg::clg_exception(std::string("a function is expected to return ") + typeid(Return).name() + "; nothing returned");
                }
                return get_from_lua<Return>(mLua);
            }
        }

        lua_State* mLua;
        clg::ref mRef;

        function(clg::ref name, lua_State* lua) : mRef(std::move(name)), mLua(lua) {}

        template<typename Arg, typename... Args>
        void push(Arg&& arg, Args&& ... args) const {
            push_to_lua(mLua, std::forward<Arg>(arg));

            push(std::forward<Args>(args)...);
        }

        void push() const noexcept {}

        void push_function_to_be_called() const noexcept {
            mRef.push_value_to_stack();
        }

        bool isNull() const noexcept {
            return mRef.isNull();
        }

        void do_call(unsigned args, int results) const {
            if (lua_pcall(mLua, args, results, 0)) {
                if (error_callback()) {
                    error_callback()();
                    return;
                }
                auto s = any_to_string(mLua);
                throw lua_exception("failed to call " + mRef.debug_str() + ": " + s);
            }
        }

        static std::function<void()>& error_callback() {
            static std::function<void()> v;
            return v;
        }
    };

    template<>
    struct converter<clg::function> {
        static clg::function from_lua(lua_State* l, int n) {
            return { l, get_from_lua<ref>(l, n) };
        }
        static int to_lua(lua_State* l, const clg::ref& ref) {
            ref.push_value_to_stack();
            return 1;
        }
    };

}