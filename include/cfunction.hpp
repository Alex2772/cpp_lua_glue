#pragma once

#include "lua.hpp"
#include "exception.hpp"


namespace clg {
    /**
     * @brief Helper type. When used as a return type, the first argument passed to a c++ function is returned to lua.
     */
    struct builder_return_type {};

    namespace detail {

        template<typename... TupleArgs>
        struct tuple_fill_from_lua_helper {
            lua_State* l;
            tuple_fill_from_lua_helper(lua_State* l) : l(l) {}

            using target_tuple = std::tuple<TupleArgs...>;

            template<unsigned index>
            void fill(target_tuple& t) {}

            template<unsigned index, typename Arg, typename... Args>
            void fill(target_tuple& t) {
                std::get<index>(t) = clg::get_from_lua<Arg>(l, index + 1);
                fill<index + 1, Args...>(t);
            }
        };

        template<typename Return, typename... Args>
        struct register_function_helper {
            using function_t = Return(*)(Args...);

            static constexpr bool is_vararg = std::is_same_v<std::tuple<Args...>, std::tuple<vararg>>;

            template<function_t f, bool passthroughSubstitutionError = false>
            struct instance {
                static int call(lua_State* s) noexcept(!passthroughSubstitutionError) {
                    clg::checkThread();
                    try {
                        size_t argsCount = lua_gettop(s);
                        std::tuple<std::decay_t<Args>...> argsTuple;

                        try {
                            if constexpr (!is_vararg) {
                                if (argsCount != sizeof...(Args)) {
                                    throw substitution_error("invalid argument count! expected "
                                                             + std::to_string(sizeof...(Args))
                                                             + ", actual " + std::to_string(argsCount));
                                }
                            }

                            tuple_fill_from_lua_helper<std::decay_t<Args>...>(s).template fill<0, std::decay_t<Args>...>(argsTuple);
                        } catch (const std::exception& e) {
                            throw substitution_error(e.what());
                        }

                        if constexpr (std::is_same_v<Return, builder_return_type>) {
                            lua_pop(s, sizeof...(Args) - 1);
                            (std::apply)(f, std::move(argsTuple));
                            return 1;
                        } else if constexpr (std::is_void_v<Return>) {
                            lua_pop(s, sizeof...(Args));
                            // ничего не возвращается
                            (std::apply)(f, std::move(argsTuple));
                            return 0;
                        } else {
                            if constexpr (!is_vararg) {
                                lua_pop(s, sizeof...(Args));
                            }
                            // возвращаем одно значение
                            return clg::push_to_lua(s, (std::apply)(f, std::move(argsTuple)));
                        }
                    } catch (const substitution_error& e) {
                        if constexpr (passthroughSubstitutionError) {
                            throw;
                        }
                        luaL_error(s, "cpp exception: %s", e.what());
                        return 0;
                    } catch (const std::exception& e) {
                        luaL_error(s, "cpp exception: %s", e.what());
                        return 0;
                    }
                }
            };
        };

        template<typename Return, typename... Args>
        static register_function_helper<Return, Args...> make_register_function_helper(Return(*)(Args...)) {
            return {};
        }
    }

    template<auto f>
    static constexpr lua_CFunction cfunction() {
        using my_register_function_helper = decltype(detail::make_register_function_helper(f));
        using my_instance = typename my_register_function_helper::template instance<f>;
        return my_instance::call;
    }
}