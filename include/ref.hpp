#pragma once

#include "lua.hpp"
#include "converter.hpp"
#include "value.hpp"
#include <cassert>
#include <algorithm>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace clg {
    namespace detail {

        inline lua_State* main_thread(lua_State* L_, lua_State* backup_if_unsupported_ = nullptr) {
            // https://github.com/ThePhD/sol2/blob/e8e122e9ce46f4f1c0b04003d8b703fe1b89755a/include/sol/reference.hpp#L220
            if (L_ == nullptr)
                return backup_if_unsupported_;
            lua_rawgeti(L_, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
            lua_State* Lmain = lua_tothread(L_, -1);
            lua_pop(L_, 1);
            return Lmain;
        }
	}
    class ref {
    public:
        ref() = default;
        ref(const ref& other) noexcept: mPtr([&] {
            if (!other.isNull()) {
                other.push_value_to_stack();
                clg::check_thread();
                return incRef();
            }
            return -1;
        }()) {}
        ref(ref&& other) noexcept: mPtr(other.mPtr) {
            other.mPtr = -1;
        }

        ref(std::nullptr_t): ref() {}

        template<typename T>
        static ref from_cpp(lua_State* lua, const T& t) {
            clg::push_to_lua(lua, t);
            return from_stack(lua);
        }

        ref& operator=(ref&& other) noexcept {
            mPtr = other.mPtr;
            other.mPtr = -1;
            return *this;
        }
        ref& operator=(const ref& other) noexcept {
            if (mPtr == other.mPtr) return *this;
            mPtr = [&] {
                if (!other.isNull()) {
                    other.push_value_to_stack();
                    clg::check_thread();
                    return incRef();
                }
                return -1;
            }();
            return *this;
        }

        ~ref() {
            releaseIfNotNull();
        }


        ref& operator=(std::nullptr_t) noexcept {
            releaseIfNotNull();
            mPtr = -1;
            return *this;
        }

        static ref from_stack(lua_State* state = clg::state()) noexcept {
            assert(("from_stack with an empty stack?", lua_gettop(state) > 0));
            return { state };
        }

        clg::ref metatable() const noexcept {
            clg::stack_integrity_check check(clg::state());
            push_value_to_stack();
            if (lua_getmetatable(clg::state(), -1)) {
                auto value = from_stack(clg::state());
                lua_pop(clg::state(), 1);
                return value;
            }
            lua_pop(clg::state(), 1);
            return nullptr;
        }

        template<typename Value>
        void set_metatable(const Value& meta) noexcept {
            clg::stack_integrity_check check(clg::state());
            push_value_to_stack();
            clg::push_to_lua(clg::state(), meta);
            lua_setmetatable(clg::state(), -2);
            lua_pop(clg::state(), 1);
        }

        void push_value_to_stack(lua_State* l = clg::state()) const noexcept {
            assert(l != nullptr);
            lua_rawgeti(l, LUA_REGISTRYINDEX, mPtr);
        }

        std::string debug_str() const noexcept {
            if (isNull()) {
                return "\"nil\"";
            }
            clg::stack_integrity_check check(clg::state());
            push_value_to_stack();
            auto s = clg::any_to_string(clg::state(), -1);
            lua_pop(clg::state(), 1);
            return s;
        }

        clg::value value() const noexcept {
            return as<clg::value>();
        }

        bool isNull() const noexcept {
            return mPtr == -1;
        }

        bool isFunction() const noexcept {
            clg::stack_integrity_check check(clg::state());
            push_value_to_stack();
            bool value = lua_isfunction(clg::state(), -1);
            lua_pop(clg::state(), 1);
            return value;
        }

        explicit operator bool() const noexcept {
            return !isNull();
        }

        template<typename T>
        T as() const {
            if constexpr (std::is_convertible_v<std::nullptr_t, T>) {
                if (isNull()) {
                    return nullptr;
                }
            } else {
                assert(!isNull());
            }
            stack_integrity_check check(clg::state());
            push_value_to_stack();

            return clg::pop_from_lua<T>(clg::state());
        }

        template<typename T>
        converter_result<T> as_converter_result() const {
            assert(!isNull());
            stack_integrity_check check(clg::state());
            push_value_to_stack();

            auto v = clg::get_from_lua_raw<T>(clg::state(), -1);
            lua_pop(clg::state(), 1);
            return v;
        }

        template<typename T>
        std::optional<T> is() const {
            if (isNull()) {
                return std::nullopt;
            }

            stack_integrity_check check(clg::state());
            auto r = as_converter_result<T>();
            if (r.is_error()) {
                return std::nullopt;
            }
            return *r;
        }

        [[nodiscard]]
        bool operator==(const clg::ref& r) const noexcept {
            stack_integrity_check check(clg::state());
            push_value_to_stack();
            r.push_value_to_stack();
            bool result = lua_compare(clg::state(), -1, -2, LUA_OPEQ) == 1;
            lua_pop(clg::state(), 2);

            return result;
        }

        [[nodiscard]]
        bool operator!=(const clg::ref& r) const noexcept {
            return !operator==(r);
        }

        [[nodiscard]]
        bool operator==(std::nullptr_t) const noexcept {
            return mPtr == -1;
        }

    private:
        int mPtr = -1;

        void releaseIfNotNull() {
            if (mPtr != -1 && !clg::is_in_exit_handler()) {
                clg::check_thread();
                luaL_unref(clg::state(), LUA_REGISTRYINDEX, mPtr);
            }
        }

        int incRef() {
            if (lua_isnil(clg::state(), -1)) {
                lua_pop(clg::state(), 1);
                return -1;
            }

            auto r = luaL_ref(clg::state(), LUA_REGISTRYINDEX);
            return r;
        }

        ref(lua_State* state) noexcept: mPtr(incRef()) {
        }
    };


    class table_view: public ref {
    public:
        using ref::ref;

        struct value_view {
        public:
            value_view(const table_view& table, const std::string_view& name) : table(table), name(name) {}

            template<typename T>
            [[nodiscard]]
            T as() const {
                const auto L = clg::state();
                table.push_value_to_stack(L);
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1);
                    throw clg_exception("not a table view");
                }
                lua_getfield(L, -1, name.data());
                auto v = clg::get_from_lua<T>(L);
                lua_pop(L, 2);
                return v;
            }

            template<typename T>
            [[nodiscard]]
            std::optional<T> is() const {
                const auto L = clg::state();
                table.push_value_to_stack(L);
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1);
                    throw clg_exception("not a table view");
                }
                lua_getfield(L, -1, name.data());
                if (lua_isnil(L, -1)) {
                    lua_pop(L, 1);
                    return std::nullopt;
                }
                auto v = clg::get_from_lua_raw<T>(L);
                lua_pop(L, 1);
                if (v.is_error()) {
                    return std::nullopt;
                }
                return *v;
            }


            [[nodiscard]]
            clg::ref ref() const {
                clg::stack_integrity_check c;
                return as<clg::ref>();
            }

            [[nodiscard]]
            explicit operator class ref() const {
                return ref();
            }

            template<typename T>
            const T& operator=(const T& t) const noexcept {
                const auto L = clg::state();
                clg::stack_integrity_check c(L);
                table.push_value_to_stack();
                lua_pushstring(L, name.data());
                clg::push_to_lua(L, t);
                lua_settable(L, -3);
                lua_pop(L, 1);
                return t;
            }

            template<typename... Args>
            void invokeNullsafe(Args&&... args);
        private:
            const table_view& table;
            std::string_view name;
        };

        table_view(ref r): ref(std::move(r)) {
        }


        template<typename K, typename V>
        void raw_set(const K& key, const V& value, lua_State* L = clg::state()) const noexcept {
            clg::stack_integrity_check c(L);
            push_value_to_stack(L);
            clg::push_to_lua(L, key);
            clg::push_to_lua(L, value);
            lua_rawset(L, -3);
            lua_pop(L, 1);
        }

        size_t size(lua_State* L = clg::state()) const noexcept {
            clg::stack_integrity_check c;
            push_value_to_stack(L);
            lua_len(L, -1);
            auto v = clg::get_from_lua<int>(L);
            lua_pop(L, 2);
            return v;
        }

        value_view operator[](std::string_view v) const {
            assert(!isNull());
            return { *this, v };
        }
    };

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

    template<>
    struct converter<clg::ref> {
        static converter_result<ref> from_lua(lua_State* l, int n) {
            lua_pushvalue(l, n);
            return clg::ref::from_stack(l);
        }
        static int to_lua(lua_State* l, const clg::ref& ref) {
            if (ref.isNull()) {
                lua_pushnil(l);
                return 1;
            }
            ref.push_value_to_stack(l);
            return 1;
        }
    };

    template<>
    struct converter<clg::table_view> {
        static converter_result<table_view> from_lua(lua_State* l, int n) {
            lua_pushvalue(l, n);
            if (!lua_istable(l, -1)) {
                return converter_error{"not a table"};
            }
            return table_view(clg::ref::from_stack(l));
        }
        static int to_lua(lua_State* l, const clg::ref& ref) {
            return clg::push_to_lua(l, ref);
        }
    };

    template<>
    struct converter<clg::userdata_view> {
        static converter_result<userdata_view> from_lua(lua_State* l, int n) {
            lua_pushvalue(l, n);
            if (!lua_isuserdata(l, -1)) {
                return converter_error{"not a userdata"};
            }
            return userdata_view(clg::ref::from_stack(l));
        }
        static int to_lua(lua_State* l, const clg::ref& ref) {
            return clg::push_to_lua(l, ref);
        }
    };
}