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
        function(clg::ref name) : mRef(std::move(name)) {}

        function() = default;
        function(function&& rhs) noexcept: mRef(std::move(rhs.mRef)) {}
        function(const function& rhs): mRef(rhs.mRef) {}

        function& operator=(function&& rhs) noexcept {
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
            const auto L = clg::state();
            push_function_to_be_called();
            if (!lua_isfunction(L, -1)) {
                lua_pop(L, 1);
                return;
            }
            push(std::forward<Args>(args)...);
            do_call(sizeof...(args), 0);
        }

        template<typename Return, typename... Args>
        Return call(Args&& ... args) const {
            const auto L = clg::state();
            lua_settop(L, 0);
            stack_integrity_fix stack(L);
            push_function_to_be_called();

            push(std::forward<Args>(args)...);

            if constexpr (std::is_same_v < Return, clg::dynamic_result >) {
                do_call(sizeof...(args), LUA_MULTRET);
                return pop_from_lua<Return>(L);
            } else if constexpr (std::is_same_v < Return, void >) {
                do_call(sizeof...(args), 0);
            } else {
                do_call(sizeof...(args), 1);
                if (lua_gettop(L) != 1) {
                    throw clg::clg_exception(std::string("a function is expected to return ") + typeid(Return).name() + "; nothing returned");
                }
                return pop_from_lua<Return>(L);
            }
        }

        clg::ref mRef;


        template<typename Arg, typename... Args>
        void push(Arg&& arg, Args&& ... args) const {
            const auto L = clg::state();
            push_to_lua(L, std::forward<Arg>(arg));

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
            const auto L = clg::state();
            // insert error handler before args
            int argsDelta = lua_gettop(L) - args;
            lua_pushcfunction(L, error_handler);
            lua_insert(L, argsDelta);


            auto status = pcall_callback() ? pcall_callback()(L, args, results, argsDelta)
                                           : lua_pcall(L, args, results, argsDelta);

            // remove inserted error handler
            lua_remove(L, argsDelta);

            if (status) {
                lua_settop(L, 0);
                throw lua_exception("failed to call " + mRef.debug_str());
            }
        }

        static std::function<void()>& error_callback() {
            static std::function<void()> v;
            return v;
        }

        static std::function<void(lua_State* l)>& exception_callback() {
            static std::function<void(lua_State* l)> v;
            return v;
        }

        using PcallFunc = std::function<int(lua_State* state, int args, int results, int errorFunc)>;
        static PcallFunc& pcall_callback() {
            static PcallFunc v;
            return v;
        }

    private:
        static int error_handler(lua_State* l) {
            clg::impl::raii_state_updater u(l);
            if (error_callback()) {
                error_callback()();
            }
            return 0;
        }
    };

    template<>
    struct converter<clg::function> {
        static converter_result<clg::function> from_lua(lua_State* l, int n) {
            auto r = get_from_lua_raw<ref>(l, n);
            if (r.is_error()) {
                return r.error();
            }
            return clg::function{ std::move(*r) };
        }
        static int to_lua(lua_State* l, const clg::ref& ref) {
            ref.push_value_to_stack(l);
            return 1;
        }
    };

}