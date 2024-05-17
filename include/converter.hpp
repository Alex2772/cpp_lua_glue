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

    std::string any_to_string(lua_State* l, int n = -1, int depth = 8, bool showMetatable = true);


    template<typename T, typename EnableIf=void>
    struct converter;

    template<typename T>
    struct converter<T, std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>>> {
        static converter_result<T> from_lua(lua_State* l, int n) {
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
                return converter_error{"not a number"};
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
        static converter_result<std::string> from_lua(lua_State* l, int n) {
            if (!lua_isstring(l, n)) {
                return converter_error{"not a string"};
            }
            std::size_t len;
            auto data = lua_tolstring(l, n, &len);
            return std::string{ data, len };
        }
        static int to_lua(lua_State* l, const std::string& v) {
            lua_pushlstring(l, v.c_str(), v.length());
            return 1;
        }
    };
    template<>
    struct converter<std::string_view> {
        static converter_result<std::string_view> from_lua(lua_State* l, int n) {
            if (!lua_isstring(l, n)) {
                return converter_error{"not a string"};
            }
            std::size_t len;
            auto data = lua_tolstring(l, n, &len);
            return std::string_view{ data, len };
        }
        static int to_lua(lua_State* l, std::string_view v) {
            lua_pushlstring(l, v.data(), v.length());
            return 1;
        }
    };

    template<>
    struct converter<lua_CFunction> {
        static converter_result<lua_CFunction> from_lua(lua_State* l, int n) {
            if (!lua_iscfunction(l, n)) {
                return converter_error{"not a cfunction"};
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
        static converter_result<const char*> from_lua(lua_State* l, int n) {
            if (!lua_isstring(l, n)) {
                return converter_error{"not a string"};
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
        static converter_result<void*> from_lua(lua_State* l, int n) {
            return nullptr;
        }
    };

    template<>
    struct converter<lua_State*> {
        static converter_result<lua_State*> from_lua(lua_State* l, int n) {
            return l;
        }
    };

    template<int N>
    struct converter<char[N]> {
        static converter_result<const char*> from_lua(lua_State* l, int n) {
            if (!lua_isstring(l, n)) {
                return converter_error{"not a string"};
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
        static converter_result<bool> from_lua(lua_State* l, int n) {
            if (!lua_isboolean(l, n)) {
                return converter_error{"not a boolean"};
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
        static converter_result<std::nullptr_t> from_lua(lua_State* l, int n) {
            if (!lua_isnil(l, n)) {
                return converter_error{"not a nil"};
            }
            return nullptr;
        }
        static int to_lua(lua_State* l, std::nullptr_t v) {
            lua_pushnil(l);
            return 1;
        }
    };

    template<typename T>
    static converter_result<T> get_from_lua_raw(lua_State* l, int index = -1) {
        // incomplete type 'clg::converter<...>' error here means converter is not defined or not reachable (missing #include)
        static_assert(std::is_same_v<decltype(converter<T>::from_lua(l, index)), converter_result<T>>, 
                      "converter<T>::from_lua is expected to return converter_result<T>");
        clg::checkThread();
        converter_result<T> t = converter<T>::from_lua(l, index);
        return t;
    }

    template<typename T>
    static T get_from_lua(lua_State* l, int index = -1) {
        clg::checkThread();

        converter_result<T> result = get_from_lua_raw<T>(l, index);
        if (result.is_error()) {
            std::string errorMessage = "converter returned converter_error result; get_from_lua";
            if (result.error().errorLiteral) {
                errorMessage += ": ";
                errorMessage += result.error().errorLiteral;
            }
            throw clg_exception(std::move(errorMessage));
        }
        return *result;
    }

    template<typename T>
    static T pop_from_lua(lua_State* l) {
        clg::checkThread();

        converter_result<T> result = get_from_lua_raw<T>(l, -1);
        lua_pop(l, 1);
        if (result.is_error()) {
            std::string errorMessage = "converter returned converter_error result; get_from_lua";
            if (result.error().errorLiteral) {
                errorMessage += ": ";
                errorMessage += result.error().errorLiteral;
            }
            throw clg_exception(std::move(errorMessage));
        }
        return *result;
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
        static converter_result<std::variant<Types...>> from_lua_recursive(lua_State* l, int n) {
            auto r = clg::get_from_lua_raw<T>(l, n);
            if (r.is_ok()) {
                return *r;
            }

            if constexpr (sizeof...(T2) != 0) {
                return from_lua_recursive<T2...>(l, n);
            }

            return converter_error{"none of variant types can be converted to"};
        }

        static converter_result<std::variant<Types...>> from_lua(lua_State* l, int n) {
            return from_lua_recursive<Types...>(l, n);
        }
        static int to_lua(lua_State* l, const std::variant<Types...>& types) {
            return std::visit([&](const auto& v) {
                return clg::push_to_lua(l, v);
            }, types);
        }
    };


    /**
     * @brief Borrows conversion routine from one type to another.
     * @example
     * @code{cpp}
     * // AngleDegrees will be converted to float as if it was float
     * template<> struct converter<AngleDegrees>: converter_derived<float, AngleDegrees> {};
     * @endcode
     */
    template<typename From, typename To>
    struct converter_derived {
    public:
        static converter_result<To> from_lua(lua_State* l, int n) {
            auto r = clg::get_from_lua_raw<From>(l, n);
            if (r.is_error()) {
                return r.error();
            }
            return To(std::move(*r));
        }

        static int to_lua(lua_State* l, const To& t) {
            return clg::push_to_lua(l, From(t));
        }
    };
}