#pragma once

#include "lua.hpp"
#include "exception.hpp"


namespace clg {
    /**
     * @brief Helper type. When used as a return type, the first argument passed to a c++ function is returned to lua.
     */
    struct builder_return_type {};


    static constexpr int OVERLOADED_HELPER_SUBSTITUTION_FAILURE = -228;
    namespace detail {

        template<typename... TupleArgs>
        struct tuple_fill_from_lua_helper {
            lua_State* l;
            tuple_fill_from_lua_helper(lua_State* l) : l(l) {}

            using target_tuple = std::tuple<TupleArgs...>;

            template<unsigned tupleIndex>
            std::optional<clg::converter_error> fill(target_tuple& t, int luaIndex = tupleIndex) {
                return std::nullopt;
            }

            template<unsigned tupleIndex, typename Arg, typename... Args>
            std::optional<clg::converter_error>fill(target_tuple& t, int luaIndex = tupleIndex) {
                clg::converter_result<Arg> r = clg::get_from_lua_raw<Arg>(l, luaIndex + 1);
                if (r.is_error()) {
                    return r.error();
                }
                std::get<tupleIndex>(t) = std::move(*r);
                if constexpr (std::is_same_v<lua_State*, Arg>) {
                    // lua_State* is not taken from lua; we should omit it's index
                    return fill<tupleIndex + 1, Args...>(t, luaIndex);
                }
                return fill<tupleIndex + 1, Args...>(t, luaIndex + 1);
            }
        };


        template<typename Return, typename... Args>
        struct register_function_helper {
            using function_t = Return(*)(Args...);

            static constexpr bool is_vararg = std::is_same_v<std::tuple<Args...>, std::tuple<vararg>>;

            template<function_t f, bool passthroughSubstitutionError = false>
            struct instance {
                static int call(lua_State* s) {
                    clg::checkThread();
                    const size_t expectedArgCount = (0 + ... + int(!std::is_same_v<lua_State*, Args>));
                    try {
                        size_t argsCount = lua_gettop(s);

                        if constexpr (!is_vararg) {
                            if (argsCount != expectedArgCount) {
                                if constexpr (passthroughSubstitutionError) {
                                    return OVERLOADED_HELPER_SUBSTITUTION_FAILURE;
                                }
                                throw clg_exception("invalid argument count! expected "
                                                            + std::to_string(sizeof...(Args))
                                                            + ", actual " + std::to_string(argsCount));
                            }
                        }

                        std::tuple<std::decay_t<Args>...> argsTuple;

                        if (auto error = tuple_fill_from_lua_helper<std::decay_t<Args>...>(s).template fill<0, std::decay_t<Args>...>(argsTuple)) {
                            if constexpr (passthroughSubstitutionError) {
                                return OVERLOADED_HELPER_SUBSTITUTION_FAILURE;
                            }
                            throw clg_exception(error->errorLiteral ? error->errorLiteral : "unknown converter failure");
                        }


                        if constexpr (std::is_same_v<Return, builder_return_type>) {
                            lua_pop(s, expectedArgCount - 1);
                            (std::apply)(f, std::move(argsTuple));
                            return 1;
                        } else if constexpr (std::is_void_v<Return>) {
                            lua_pop(s, expectedArgCount);
                            // ничего не возвращается
                            (std::apply)(f, std::move(argsTuple));
                            return 0;
                        } else {
                            if constexpr (!is_vararg) {
                                lua_pop(s, expectedArgCount);
                            }
                            // возвращаем одно значение
                            return clg::push_to_lua(s, (std::apply)(f, std::move(argsTuple)));
                        }
                    } catch (const std::exception& e) {
                        if (clg::function::exception_callback()) {
                            clg::function::exception_callback()(s);
                        }
                        clg::push_to_lua(s, nullptr);
                        clg::push_to_lua(s, e.what());
                        return 2;
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