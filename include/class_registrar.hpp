//
// Created by Alex2772 on 11/3/2021.
//

#pragma once

#include "clg.hpp"
#include "table.hpp"
#include <vector>
#include <cassert>
#include <cstdio>

namespace clg {
    namespace impl {
#if LUA_VERSION_NUM == 501
        static void newlib(lua_State* L, const std::vector<luaL_Reg>& l) {
            lua_createtable(L, 0, l.size() - 1);
            luaL_setfuncs(L, l.data(), 0);
        }
#else
    static void newlib(lua_State* L, const std::vector<luaL_Reg>& l) {
        luaL_newlib(L, const_cast<std::vector<luaL_Reg>&>(l).data());
    }
#endif
        static void newlib(lua_State* L, const lua_cfunctions& l) {
            std::vector<luaL_Reg> reg;
            reg.reserve(l.size() + 1);
            for (const auto& c : l) {
                reg.push_back({c.name.c_str(), c.cFunction});
            }
            reg.push_back({nullptr, nullptr});
            newlib(L, reg);
        }

        template<typename T>
        static clg::ref table_from_c_functions(lua_State* L, const T& l) {
            clg::stack_integrity_check check(L);
            clg::impl::newlib(L, l);
            return clg::ref::from_stack(L);
        }

    }

    template<class C>
    class class_registrar {
    friend class clg::state_interface;
    private:
        state_interface& mClg;

        class_registrar(state_interface& clg):
            mClg(clg)
        {

        }

        lua_cfunctions mMethods;
        lua_cfunctions mStaticFunctions;
        lua_cfunctions mMetaFunctions;
        std::vector<lua_CFunction> mConstructors;

        template<auto methodPtr>
        struct method_helper {
            using class_info = state_interface::callable_class_info<decltype(methodPtr)>;

            template<typename... Args>
            struct wrapper_function_helper_t {};
            template<typename... Args>
            struct wrapper_function_helper_t<state_interface::types<Args...>> {
                static typename class_info::return_t method(std::shared_ptr<C> self, Args... args) {
                    if (!self) {
                        throw clg_exception("attempt to call class method for a nil value");
                    }
                    if (std::is_same_v<void, typename class_info::return_t>) {
                        (self.get()->*methodPtr)(std::move(args)...);
                    } else {
                        return (self.get()->*methodPtr)(std::move(args)...);
                    }
                }
                static clg::builder_return_type builder_method(std::shared_ptr<C> self, Args... args) {
                    if (!self) {
                        throw clg_exception("attempt to call class method for a nil value");
                    }
                    (self.get()->*methodPtr)(std::move(args)...);
                    return {};
                }
                using my_instance = typename clg::detail::register_function_helper<typename class_info::return_t, std::shared_ptr<C>, Args...>::template instance<method>;
                using my_instance_builder = typename clg::detail::register_function_helper<clg::builder_return_type, std::shared_ptr<C>, Args...>::template instance<builder_method>;
            };

            using wrapper_function_helper = wrapper_function_helper_t<typename class_info::args>;
        };

        template<auto method>
        struct static_function_helper {
            using class_info = state_interface::callable_class_info<decltype(method)>;

            template<typename... Args>
            struct wrapper_function_helper_t {};
            template<typename... Args>
            struct wrapper_function_helper_t<state_interface::types<Args...>> {
                static typename class_info::return_t static_method(void* self, Args... args) {
                    if (std::is_same_v<void, typename class_info::return_t>) {
                        method(std::move(args)...);
                    } else {
                        return method(std::move(args)...);
                    }
                }
                static typename class_info::return_t static_method_no_this(Args... args) {
                    if (std::is_same_v<void, typename class_info::return_t>) {
                        method(std::move(args)...);
                    } else {
                        return method(std::move(args)...);
                    }
                }
                using my_instance = typename clg::detail::register_function_helper<typename class_info::return_t, void*, Args...>::template instance<static_method>;
                using my_instance_no_this = typename clg::detail::register_function_helper<typename class_info::return_t, Args...>::template instance<static_method_no_this>;
            };

            using wrapper_function_helper = wrapper_function_helper_t<typename class_info::args>;
        };

        struct brackets_operator_helper {
            inline static lua_CFunction func = nullptr;
            inline static std::optional<clg::ref> methods;

            static int handler(lua_State* L) {
                assert(func != nullptr && methods);
                if (lua_isstring(L, 2) && !lua_isnumber(L, 2)) {
                    auto method_name = get_from_lua<std::string_view>(L, 2);
                    push_to_lua(L, methods->as<table>()[method_name]);
                    if (!lua_isnil(L, -1)) {
                        return 1;
                    }

                    lua_pop(L, 1);
                }

                lua_pushcfunction(L, func);
                lua_pushvalue(L, 1);
                lua_pushvalue(L, 2);
                lua_call(L, 2, 1);
                return 1;
            };
        };

        template<typename... Args>
        struct constructor_helper {
            static std::shared_ptr<C> construct(void* self, Args... args) {
                return std::make_shared<C>(std::move(args)...);
            }
        };

        static int gc(lua_State* l) {
            if (lua_isuserdata(l, 1)) {
                static_cast<clg::impl::dealloc_helper*>(lua_touserdata(l, 1))->~dealloc_helper();
            }
            return 0;
        }
        static int eq(lua_State* l) {
            auto v1 = get_from_lua<std::shared_ptr<C>>(l, 1);
            auto v2 = get_from_lua<std::shared_ptr<C>>(l, 2);
            push_to_lua(l, v1 == v2);
            return 1;
        }
        static int concat(lua_State* l) {
            auto v1 = any_to_string(l, 1);
            auto v2 = any_to_string(l, 2);
            v1 += v2;
            push_to_lua(l, v1);
            return 1;
        }
        static int tostring(lua_State* l) {
            auto v1 = get_from_lua<std::shared_ptr<C>>(l, 1);
            push_to_lua(l, toString(v1));
            return 1;
        }

        static std::string toString(const std::shared_ptr<C>& v) {
            char buf[64];
            std::sprintf(buf, "%s<%p>", class_name<C>().c_str(), v.get());
            return buf;
        }

    public:
        ~class_registrar() {
            mClg.class_metainfo();

            std::vector<luaL_Reg> staticFunctions;
            clg::stack_integrity_check check(mClg);

            if constexpr (std::is_base_of_v<clg::allow_lua_inheritance, C>) {
                if constexpr(!std::is_abstract_v<C>) {
                    C instance;
                    for (const auto& e: mClg.class_metainfo()) {
                        if (e.isBaseOf(&instance)) {
                            for (const auto& method : e.methods) {
                                auto it = std::find_if(mMethods.begin(), mMethods.end(), [&](const auto& v) {
                                    return v.name == method.name;
                                });
                                if (it == mMethods.end()) {
                                    mMethods.push_back(method);
                                } else {
                                    *it = method;
                                }
                            }
                        }
                    }
                }
            }

            for (auto& c : mConstructors) {
                staticFunctions.push_back({"new", c});
            }
            for (auto& c : mStaticFunctions) {
                staticFunctions.push_back({c.name.c_str(), c.cFunction});
            }

            staticFunctions.push_back({nullptr, nullptr});

            const auto classname = clg::class_name<C>();

            auto clazz = impl::table_from_c_functions(mClg, staticFunctions);

            std::vector<luaL_Reg> metatableFunctions = {
                    { "__gc", gc },
                    { "__eq", eq },
                    { "__concat", concat },
                    { "__tostring", tostring },
            };
            metatableFunctions.reserve(metatableFunctions.size() + mMetaFunctions.size() + 1);
            for (const auto& v : mMetaFunctions) {
                metatableFunctions.push_back({v.name.c_str(), v.cFunction});
            }

            metatableFunctions.push_back({nullptr});

            clg::table_view metatable = impl::table_from_c_functions(mClg, metatableFunctions);

            auto methods = impl::table_from_c_functions(mClg, mMethods);

            if (brackets_operator_helper::func) {
                metatable["__index"] = static_cast<lua_CFunction>(brackets_operator_helper::handler);
                brackets_operator_helper::methods = std::move(methods);
            }
            else {
                metatable["__index"] = methods;
            }

            clazz.set_metatable(metatable);

            mClg.set_global_value(classname, clazz);

            if constexpr (std::is_base_of_v<clg::allow_lua_inheritance, C>) {
                mClg.class_metainfo().push_back({
                    [](clg::allow_lua_inheritance* probe) {
                        return dynamic_cast<clg::allow_lua_inheritance*>(probe) != nullptr;
                    },
                    std::move(mMethods)
                });
            }
        }


        template<typename... Args>
        class_registrar<C>& constructor() noexcept {
            using my_register_function_helper = clg::detail::register_function_helper<std::shared_ptr<C>, void*, Args...>;
            using my_instance = typename my_register_function_helper::template instance<constructor_helper<Args...>::construct>;

            mConstructors.push_back({
                my_instance::call
            });
            return *this;
        }

        template<auto m>
        class_registrar<C>& method(std::string name) {
            using wrapper_function_helper = typename method_helper<m>::wrapper_function_helper;
            using my_instance = typename wrapper_function_helper::my_instance;
            mMethods.push_back({
               std::move(name),
               my_instance::call
            });
            return *this;
        }
        template<typename Callable>
        class_registrar<C>& method(std::string name, Callable&& callable) {
            mMethods.push_back({
               std::move(name),
               mClg.wrap_lambda_to_cfunction(std::forward<Callable>(callable))
            });
            return *this;
        }

        template<auto m>
        class_registrar<C>& builder_method(std::string name) {
            using wrapper_function_helper = typename method_helper<m>::wrapper_function_helper;
            using my_instance = typename wrapper_function_helper::my_instance_builder;
            mMethods.push_back({
               std::move(name),
               my_instance::call
            });
            return *this;
        }

        template<auto m>
        class_registrar<C>& staticFunction(std::string name) {
            using wrapper_function_helper = typename static_function_helper<m>::wrapper_function_helper;

#if LUA_VERSION_NUM == 501
            constexpr auto call = wrapper_function_helper::my_instance_no_this::call;
#else
            constexpr auto call = wrapper_function_helper::my_instance_no_this::call;
#endif
            mStaticFunctions.push_back({
               std::move(name),
               call
            });
            return *this;
        }

        template<typename Callable>
        class_registrar<C>& staticFunction(std::string name, Callable&& callback) {
            auto wrap = mClg.wrap_lambda_to_cfunction(std::forward<Callable>(callback));

            mStaticFunctions.push_back({
               std::move(name),
               wrap
            });
            return *this;
        }

        template<typename Callable>
        class_registrar<C>& meta(std::string name, Callable&& callback) {
            auto wrap = mClg.wrap_lambda_to_cfunction(std::forward<Callable>(callback));

            mMetaFunctions.push_back({
                                               std::move(name),
                                               wrap
                                       });
            return *this;
        }

        template<auto m>
        class_registrar<C>& meta(std::string name) {
            using wrapper_function_helper = typename static_function_helper<m>::wrapper_function_helper;

#if LUA_VERSION_NUM == 501
            constexpr auto call = wrapper_function_helper::my_instance_no_this::call;
#else
            constexpr auto call = wrapper_function_helper::my_instance_no_this::call;
#endif
            mMetaFunctions.push_back({
                 std::move(name),
                 call
            });
            return *this;
        }

        template<auto m>
        class_registrar<C>& bracketsOperator() {
            using wrapper_function_helper = typename method_helper<m>::wrapper_function_helper;
            using my_instance = typename wrapper_function_helper::my_instance;
            brackets_operator_helper::func = my_instance::call;
            return *this;
        }

    };
}
