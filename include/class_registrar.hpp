//
// Created by Alex2772 on 11/3/2021.
//

#pragma once

#include "clg.hpp"
#include "table.hpp"
#include "object_expose.hpp"
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
        static clg::table_view table_from_c_functions(lua_State* L, const T& l) {
            clg::stack_integrity_check check(L);
            clg::impl::newlib(L, l);
            return clg::ref::from_stack(L);
        }

        static void switch_to_registry_state(lua_State* l, int index) {
        	index = lua_absindex(l, index);
      		auto helper = static_cast<userdata_helper*>(lua_touserdata(l, index));
        	if (!helper->is_strong_ptr_stored()) {
            	return;
        	}
    		auto self = helper->as_lua_self();
    		assert(self != nullptr);
    		clg::state_interface s(l);
    		auto clazz = s.global_variable(clg::class_name<C>());
    	    assert(!clazz.isNull());
    	    auto clazzMeta = clazz.metatable();
    	    assert(!clazzMeta.isNull());
    	    auto meta = clazzMeta.template as<table_view>()["__clg_metatable"].ref();
    	    assert(!meta.isNull());
    	    meta.push_value_to_stack(l);
    	    assert(lua_type(l, -1) == LUA_TTABLE);
    	    lua_setmetatable(l, index); // restore metatable, mark object for re-finalization, see https://www.lua.org/manual/5.4/manual.html#2.5.3
    	    lua_pushvalue(l, index);    // duplicate uservalue
    	    impl::update_strong_userdata(*self, clg::ref::from_stack(l)); // save object in global registry, permanent resurrect
    	    auto b = helper->switch_to_weak(); // switching to weak_ptr to avoid cyclic links
    	    assert(b);
        }
    }

    /**
	* @brief Forces changing clg userdata state to registry state
	* @note clg at the moment can't handle registry links to clg userdata properly, it may lead to memory links,
	*		this function helps to resolve these links (i.e. custom garbage collector cycle). You should force switching
	*		to registry state every userdata that is not reachable in regular lua usage.
	*/
    inline void forceSwitchToRegistryState(const std::shared_ptr<void>& object) {
    	clg::push_to_lua(clg::state(), object);
      	auto helper = static_cast<userdata_helper*>(lua_touserdata(l, -1));
        if (!helper->is_strong_ptr_stored()) {
            return;
        }
    	switch_to_registry_state(l, index);
    }

    template<class C>
    class class_registrar {
    friend class clg::state_interface;
    private:
        state_interface& mClg;

        class_registrar(state_interface& clg) : mClg(clg) {
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

        template<typename... Args>
        struct constructor_helper {
            static std::shared_ptr<C> construct(void* self, Args... args) {
                return std::make_shared<C>(std::move(args)...);
            }
        };

        static int gc(lua_State* l) {
            clg::impl::raii_state_updater u(l);
            clg::stack_integrity_check c(l, 0);
            assert(lua_isuserdata(l, 1));
            auto helper = static_cast<userdata_helper*>(lua_touserdata(l, 1));
            if (lua_getuservalue(l, 1) != LUA_TNIL) {
                lua_pop(l, 1);
                // use lua_self for the userdata, memory management is not trivial in this case
                if (!helper->expired() && !is_in_exit_handler()) {
                    impl::switchToRegistryState(l, 1);
                }
                else {
                    // associated object is dead, helper is not needed anymore, call destructor of helper
                    helper->~userdata_helper();
                }
            }
            else {
                lua_pop(l, 1);
                // just call destructor of helper
                helper->~userdata_helper();
            }

            return 0;
        }

        static int eq(lua_State* l) {
            clg::impl::raii_state_updater u(l);
            stack_integrity_check c(l, 1);
            auto v1 = get_from_lua_raw<std::shared_ptr<C>>(l, 1);
            if (!v1.is_ok()) {
                push_to_lua(l, false);
                return 1;
            }
            auto v2 = get_from_lua_raw<std::shared_ptr<C>>(l, 2);
            if (!v2.is_ok()) {
                push_to_lua(l, false);
                return 1;
            }
            push_to_lua(l, *v1 == *v2);
            return 1;
        }
        static int concat(lua_State* l) {
            clg::impl::raii_state_updater u(l);
            stack_integrity_check c(l, 1);
            auto v1 = any_to_string(l, 1);
            auto v2 = any_to_string(l, 2);
            v1 += v2;
            push_to_lua(l, v1);
            return 1;
        }
        static int tostring(lua_State* l) {
            clg::impl::raii_state_updater u(l);
            stack_integrity_check c(l, 1);
            assert(lua_isuserdata(l, 1));
            auto v1 = get_from_lua<std::shared_ptr<C>>(l, 1);
            push_to_lua(l, toString(v1));
            return 1;
        }

        static std::string toString(const std::shared_ptr<C>& v) {
            char buf[64];
            std::sprintf(buf, "%s<%p>", class_name<C>().c_str(), v.get());
            return buf;
        }

        static int index(lua_State* l) {
            clg::impl::raii_state_updater u(l);
            clg::stack_integrity_check c(l, 1);
            assert(lua_isuserdata(l, 1));

            if (lua_getuservalue(l, 1) != LUA_TNIL) {
                lua_pushvalue(l, 2);    // push key to stack
                if (lua_rawget(l, -2) != LUA_TNIL) { // trying to get value (pops value from stack)
                    lua_remove(l, -2);      // remove table from stack
                    return 1;
                }
                lua_pop(l, 2);
            }
            else {
                lua_pop(l, 1);
            }

            if (lua_isstring(l, 2)) {   // is key is not a string, we have no need to index method table
                // trying to index method table
                if (lua_getmetatable(l, 1)) {               // get metatable of userdata
                    lua_pushstring(l, "__clg_methods");
                    if (lua_rawget(l, -2) == LUA_TTABLE) {  // get table with registered methods
                        lua_remove(l, -2);                  // remove metatable
                        lua_pushvalue(l, 2);                // push key to stack
                        lua_rawget(l, -2);                  // indexing table of methods
                        lua_remove(l, -2);                  // remove table with registered methods
                        return 1;
                    }
                    else {
                        lua_pop(l, 1);  // remove metatable
                    }
                }
            }

            lua_pushnil(l);
            return 1;
        }

        static int newindex(lua_State* l) {
            clg::impl::raii_state_updater u(l);
            clg::stack_integrity_check c(l, 0);
            if (!lua_isuserdata(l, 1)) {
                return luaL_error(l, "metamathod __newindex of clg userdata is applicable to userdata only");
            }
            auto userdata = static_cast<userdata_helper*>(lua_touserdata(l, 1));

            // if we have ref to userdata in lua, we store data holder in uservalue slot, try to get it
            if (lua_getuservalue(l, 1) != LUA_TTABLE) {
                return luaL_error(l, "attempt to index clg userdata value without uservalue set (possibly not inherited from clg::lua_self)");
            }
            auto self = userdata->as_lua_self();
            lua_pushvalue(l, 2);    // push key
            lua_pushvalue(l, 3);    // push value
            lua_rawset(l, -3);      // add value to data holder table
            lua_pop(l, 1);          // pop data holder table
            if (lua_isstring(l, 2) && lua_isfunction(l, 3)) {
                lua_pushvalue(l, 3);
                impl::invoke_handle_lua_virtual_func_assignment(*self, lua_tostring(l, 2), clg::ref::from_stack(l));
            }
            return 0;
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
                    { "__index", index },
                    { "__newindex", newindex}
            };
            metatableFunctions.reserve(metatableFunctions.size() + mMetaFunctions.size() + 1);
            for (const auto& v : mMetaFunctions) {
                metatableFunctions.push_back({v.name.c_str(), v.cFunction});
            }

            metatableFunctions.push_back({nullptr});

            clg::table_view metatable = impl::table_from_c_functions(mClg, metatableFunctions);

            auto methods = impl::table_from_c_functions(mClg, mMethods);
            metatable["__clg_methods"] = methods;
            clazz.set_metatable(clg::table{
                {"__index", std::move(methods)},
                {"__clg_metatable", std::move(metatable)}
            });

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
#if CLG_TRACE_CALLS
            my_instance::trace_name() = name;
#endif
            mMethods.push_back({
               std::move(name),
               my_instance::call
            });
            return *this;
        }
        template<typename Callable>
        class_registrar<C>& method(std::string name, Callable&& callable) {
            auto v = mClg.wrap_lambda_to_cfunction(std::forward<Callable>(callable), name);
            mMethods.push_back({
               std::move(name),
               v
            });
            return *this;
        }

        template<auto m>
        class_registrar<C>& builder_method(std::string name) {
            using wrapper_function_helper = typename method_helper<m>::wrapper_function_helper;
            using my_instance = typename wrapper_function_helper::my_instance_builder;
#if CLG_TRACE_CALLS
            my_instance::trace_name() = name;
#endif
            mMethods.push_back({
               std::move(name),
               my_instance::call
            });
            return *this;
        }

        template<auto m>
        class_registrar<C>& staticFunction(std::string name) {
            using wrapper_function_helper = typename static_function_helper<m>::wrapper_function_helper;

            using my_instance = typename wrapper_function_helper::my_instance_no_this;

#if CLG_TRACE_CALLS
            my_instance::trace_name() = name;
#endif

            constexpr auto call = my_instance::call;
            mStaticFunctions.push_back({
               std::move(name),
               call
            });
            return *this;
        }

        template<typename Callable>
        class_registrar<C>& staticFunction(std::string name, Callable&& callback) {
            auto wrap = mClg.wrap_lambda_to_cfunction(std::forward<Callable>(callback), name);

            mStaticFunctions.push_back({
               std::move(name),
               wrap
            });
            return *this;
        }

        template<typename Callable>
        class_registrar<C>& meta(std::string name, Callable&& callback) {
            auto wrap = mClg.wrap_lambda_to_cfunction(std::forward<Callable>(callback), name);

            mMetaFunctions.push_back({
                                               std::move(name),
                                               wrap
                                       });
            return *this;
        }

        template<auto m>
        class_registrar<C>& meta(std::string name) {
            using wrapper_function_helper = typename static_function_helper<m>::wrapper_function_helper;

            using my_instance = typename wrapper_function_helper::my_instance_no_this;

#if CLG_TRACE_CALLS
            my_instance::trace_name() = name;
#endif

            constexpr auto call = my_instance::call;
            mMetaFunctions.push_back({
                 std::move(name),
                 call
            });
            return *this;
        }
    };
}
