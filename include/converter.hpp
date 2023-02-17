//
// Created by alex2 on 02.05.2021.
//

#pragma once

#include "lua.hpp"
#include "exception.hpp"
#include "util.hpp"
#include "shared_ptr_helper.hpp"
#include <tuple>
#include <variant>

namespace clg {

    std::string any_to_string(lua_State* l, int n = -1);

    struct converter_error: clg_exception {
        using clg_exception::clg_exception;
    };


    namespace detail {
        static void throw_converter_error(lua_State* l, int n, const std::string& message) {
            throw converter_error(message + ": " + any_to_string(l, n));
        }
    }

    template<typename T, typename EnableIf=void>
    struct converter {
        static T from_lua(lua_State* l, int n) {
            if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>) {
#ifdef lua_isinteger
                if (lua_isinteger(l, n)) {
                    return static_cast<T>(lua_tointeger(l, n));
                }
#endif
                if (lua_isboolean(l, n)) {
                    return static_cast<T>(lua_toboolean(l, n));
                }
                if (lua_isnumber(l, n)) {
                    return static_cast<T>(lua_tonumber(l, n));
                }
                detail::throw_converter_error(l, n, "not a number");
            }
            throw clg_exception("unimplemented converter for " + clg::class_name<T>());
        }
        static int to_lua(lua_State* l, const T& v) {
            if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
                if constexpr (std::is_same_v<T, bool>) {
                    lua_pushboolean(l, static_cast<lua_Integer>(v));
                } else {
                    lua_pushinteger(l, static_cast<lua_Integer>(v));
                }
                return 1;
            } else if constexpr (std::is_floating_point_v<T>) {
                lua_pushnumber(l, v);
                return 1;
            }
            throw clg_exception("unimplemented converter for " + clg::class_name<T>());
        }
    };

    template<>
    struct converter<std::string> {
        static std::string from_lua(lua_State* l, int n) {
            if (!lua_isstring(l, n)) {
                detail::throw_converter_error(l, n, "not a string");
            }
            return lua_tostring(l, n);
        }
        static int to_lua(lua_State* l, const std::string& v) {
            lua_pushstring(l, v.c_str());
            return 1;
        }
    };
    template<>
    struct converter<std::string_view> {
        static std::string_view from_lua(lua_State* l, int n) {
            if (!lua_isstring(l, n)) {
                detail::throw_converter_error(l, n, "not a string");
            }
            return lua_tostring(l, n);
        }
        static int to_lua(lua_State* l, std::string_view v) {
            lua_pushstring(l, std::string(v).c_str());
            return 1;
        }
    };

    template<>
    struct converter<lua_CFunction> {
        static lua_CFunction from_lua(lua_State* l, int n) {
            if (!lua_iscfunction(l, n)) {
                detail::throw_converter_error(l, n, "not a cfunction");
            }
            return lua_tocfunction(l, n);
        }
        static int to_lua(lua_State* l, lua_CFunction v) {
            lua_pushcfunction(l, v);
            return 1;
        }
    };

    template<>
    struct converter<const char*> {
        static const char* from_lua(lua_State* l, int n) {
            if (!lua_isstring(l, n)) {
                detail::throw_converter_error(l, n, "not a string");
            }
            return lua_tostring(l, n);
        }
        static int to_lua(lua_State* l, const char* v) {
            lua_pushstring(l, v);
            return 1;
        }
    };


    template<>
    struct converter<void*> {
        static void* from_lua(lua_State* l, int n) {
            return nullptr;
        }
    };
    template<int N>
    struct converter<char[N]> {
        static const char* from_lua(lua_State* l, int n) {
            if (!lua_isstring(l, n)) {
                detail::throw_converter_error(l, n, "not a string");
            }
            return lua_tostring(l, n);
        }
        static int to_lua(lua_State* l, const char* v) {
            lua_pushstring(l, v);
            return 1;
        }
    };
    template<>
    struct converter<bool> {
        static bool from_lua(lua_State* l, int n) {
            if (!lua_isboolean(l, n)) {
                detail::throw_converter_error(l, n, "not a boolean");
            }
            return lua_toboolean(l, n);
        }
        static int to_lua(lua_State* l, bool v) {
            lua_pushboolean(l, v);
            return 1;
        }
    };

    template<>
    struct converter<std::nullptr_t> {
        static std::nullptr_t from_lua(lua_State* l, int n) {
            if (!lua_isnil(l, n)) {
                detail::throw_converter_error(l, n, "not a nil");
            }
            return nullptr;
        }
        static int to_lua(lua_State* l, std::nullptr_t v) {
            lua_pushnil(l);
            return 1;
        }
    };

    template<typename T>
    static T get_from_lua(lua_State* l) {
        clg::checkThread();
        T t = converter<T>::from_lua(l, -1);
        lua_pop(l, 1);
        return t;
    }

    template<typename T>
    static T get_from_lua(lua_State* l, unsigned index) {
        clg::checkThread();
        return converter<T>::from_lua(l, index);
    }

    /**
     * @tparam T
     * @param l
     * @param value
     * @return количество запушенных значений
     */
    template<typename T>
    static int push_to_lua(lua_State* l, const T& value) {
        clg::checkThread();
        return converter<T>::to_lua(l, value);
    }

    template<typename... Args>
    struct converter<std::tuple<Args...>> {
        static int to_lua(lua_State* l, const std::tuple<Args...>& v) {
            converter<std::tuple<Args...>> t(l);
            (std::apply)([&](Args... a) {
                t.push(std::forward<Args>(a)...);
            }, v);
            return sizeof...(Args);
        }

    private:
        lua_State* mState;

        converter(lua_State* state) : mState(state) {}

        template<typename Arg, typename... MyArgs>
        void push(Arg&& arg, MyArgs&&... args) {
            push_to_lua(mState, arg);

            push(std::forward<MyArgs>(args)...);
        }

        void push() {}
    };

    template<typename... Types>
    struct converter<std::variant<Types...>> {
        template<typename T, typename... T2>
        static std::variant<Types...> from_lua_recursive(lua_State* l, int n) {
            try {
                return clg::get_from_lua<T>(l, n);
            } catch (...) {
                if constexpr (sizeof...(T2) == 0) {
                    throw clg::converter_error("unable to convert to variant type");
                } else {
                    return from_lua_recursive<T2...>(l, n);
                }
            }
        }

        static std::variant<Types...> from_lua(lua_State* l, int n) {
            return from_lua_recursive<Types...>(l, n);
        }
        static int to_lua(lua_State* l, const std::variant<Types...>& types) {
            return std::visit([&](const auto& v) {
                return clg::push_to_lua(l, v);
            }, types);
        }
    };
}