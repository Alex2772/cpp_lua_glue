#pragma once

<<<<<<< HEAD
#include <cstring>
#include "lauxlib.h"
#include "lua.h"
#include "lua.hpp"
#include "ref.hpp"
#include "shared_ptr_helper.hpp"
=======
#include "lua.hpp"
#include "weak_ref.hpp"
>>>>>>> main
#include "table.hpp"
#include "weak_ref.hpp"

namespace clg {
    class lua_self;
    namespace impl {
        inline void invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value);
    }

    /**
     * @brief When extended from, allows to avoid extra overhead when passed to lua. Also allows lua code to use the
     * object as a table.
     * @tparam T The derived type (see example)
     * @details
     * @code{cpp}
     * class Person: clg::lua_self<Person> {
     * public:
     *   // ...
     * };
     * @endcode
     *
     * In lua:
     * @code{lua}
     * p = Person:new()
     * p['myCustomField'] = 'my extra data'
     * @endcode
     */
    class lua_self {
        template<typename T, typename EnableIf>
        friend struct converter_shared_ptr_impl;
        friend void impl::invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value);
    public:

        table_view luaDataHolder() const noexcept {
            return luaSelf();
        }


    protected:

        clg::ref luaSelf() const noexcept {
            return mLuaRepresentation.lock();
        }

        virtual void handle_lua_virtual_func_assignment(std::string_view name, clg::ref value) {}

    private:
        clg::weak_ref mLuaRepresentation;
        // lua_State* mOriginLuaState = nullptr; // just to check coroutine stuff

    };
    inline void impl::invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value) {
        s.handle_lua_virtual_func_assignment(name, std::move(value));
    }

    /**
     * userdata
     */
    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr_impl {
        static constexpr bool use_lua_self = std::is_base_of_v<clg::lua_self, T>;

        static converter_result<std::shared_ptr<T>> from_lua(lua_State* l, int n) {
            stack_integrity_check check(l);
            if (lua_isnil(l, n)) {
                return std::shared_ptr<T>(nullptr);
            }

            if constexpr(use_lua_self) {
                if (lua_istable(l, n)) {
                    if (luaL_getmetafield(l, n, "__index") == 0) {
                        // If the object does not have a metatable, or if the metatable does not have this field,
                        // returns 0 and pushes nothing.
                        return clg::converter_error{"not a cpp object"};
                    }
                    if (!lua_isuserdata(l, -1)) {
                        lua_pop(l, 1);
                        return clg::converter_error{"not a cpp object"};
                    }
                    auto p = reinterpret_cast<shared_ptr_helper*>(lua_touserdata(l, -1))->as<T>();
                    lua_pop(l, 1);
                    return p;
                }
                return std::shared_ptr<T>(nullptr);
            } else {
                if (lua_isuserdata(l, n)) {
                    return reinterpret_cast<shared_ptr_helper*>(lua_touserdata(l, n))->as<T>();
                }
                return clg::converter_error{"not a userdata"};
            }
        }

        static void push_shared_ptr_userdata(lua_State* l, std::shared_ptr<T> v) {
            clg::stack_integrity_check c(l, 1);
            auto classname = clg::class_name<T>();
            auto t = reinterpret_cast<shared_ptr_helper*>(lua_newuserdata(l, sizeof(shared_ptr_helper)));
            new(t) shared_ptr_helper(std::move(v));

#if LUA_VERSION_NUM != 501
            auto r = lua_getglobal(l, classname.c_str());
#else
            lua_getglobal(l, classname.c_str());
            auto r = lua_type(l, -1);
#endif
            if (r != LUA_TNIL)
            {
                if (lua_getmetatable(l, -1)) {
                    lua_setmetatable(l, -3);
                }
                lua_pop(l, 1);
            }
        }

        static void push_weak_ptr_userdata(lua_State* l, std::weak_ptr<T> v) {
            clg::stack_integrity_check c(l, 1);
            auto classname = clg::class_name<T>();
            auto t = reinterpret_cast<weak_ptr_helper*>(lua_newuserdata(l, sizeof(weak_ptr_helper)));
            new(t) weak_ptr_helper(std::move(v));

#if LUA_VERSION_NUM != 501
            auto r = lua_getglobal(l, classname.c_str());
#else
            lua_getglobal(l, classname.c_str());
            auto r = lua_type(l, -1);
#endif
            if (r != LUA_TNIL)
            {
                if (lua_getmetatable(l, -1)) {
                    lua_setmetatable(l, -3);
                }
                lua_pop(l, 1);
            }
        }

        static void push_strong_ref_holder_object(lua_State* l, std::shared_ptr<T> v, clg::ref dataHolderRef) {
            push_shared_ptr_userdata(l, std::move(v));
            clg::push_to_lua(l, clg::table{{"clg_strongref", clg::ref::from_stack(l)}});
            clg::push_to_lua(l, clg::table{
                { "__index", dataHolderRef },
                { "__newindex", std::move(dataHolderRef) },
            });
            lua_setmetatable(l, -2);
        }

        static void handle_virtual_func(clg::table_view table, std::string_view key, clg::ref value) {
            clg::table_view(clg::table_view(table.metatable())["__index"].ref())[key] = value;
            const auto L = clg::state();
            if constexpr (use_lua_self) {
                try {
                    clg::stack_integrity_check check(L);
                    if (!value.isFunction()) {
                        return;
                    }

                    table.push_value_to_stack(L);
                    luaL_getmetafield(L, -1, "__index");

                    if (!lua_isuserdata(L, -1)) {
                        lua_pop(L, 2);
                        return;
                    }

                    auto r = reinterpret_cast<shared_ptr_helper*>(lua_touserdata(L, -1))->as<T>();
                    if (r.is_error()) {
                        throw std::runtime_error("failed to convert from shared_ptr_helper to as<T>");
                    }
                    auto& object = *r;

                    impl::invoke_handle_lua_virtual_func_assignment(*(object.get()), key, std::move(value));
                    lua_pop(L, 2);
                } catch (...) {}
            }
        };

        static int handle_gc(lua_State* l) {
            clg::impl::raii_state_updater u(l);
            if (lua_istable(l, 1)) {
                // TODO: if lua's data is empty, we don't need to store it somewhere

                luaL_getmetafield(l, 1, "__index");
                auto& helper = *static_cast<shared_ptr_helper*>(lua_touserdata(l, -1));
                lua_pop(l, 1);
                if (helper.ptr.use_count() == 1) {
                    helper.~shared_ptr_helper();
                    return 0;
                }

                // renew the weak reference
                auto sharedPtr = (*helper.as<T>());
                auto strongRefToRepr = clg::ref::from_stack(l);
                assert(!strongRefToRepr.isNull());
                sharedPtr->mLuaRepresentation.emplace(strongRefToRepr, l);

                // block lua from destroying the representation on this gc iteration
                lua_getglobal(l, "__clg_gc_block");
                if (lua_isnil(l, -1)) {
                    lua_pop(l, 1);
                    lua_createtable(l, 0, 1024);

                    lua_pushvalue(l, -1);
                    lua_setglobal(l, "__clg_gc_block");
                }
                
                strongRefToRepr.push_value_to_stack(l); // k
                lua_pushinteger(l, 0); // v
                lua_settable(l, -3);
                return 0;
            }
            return 0;
        }

        static int to_lua(lua_State* l, std::shared_ptr<T> v) {
            if (v == nullptr) {
                lua_pushnil(l);
                return 1;
            }

            if constexpr(!use_lua_self) {
                push_shared_ptr_userdata(l, std::move(v));
            } else {
                if (auto luaRepresentation = v->mLuaRepresentation.lock(); !luaRepresentation.isNull()) {
                    luaRepresentation.push_value_to_stack(l);
                    return 1;
                }
                // if (v->mOriginLuaState != nullptr) {
                //     assert(v->mOriginLuaState == l);
                // } else {
                //     v->mOriginLuaState = l;
                // }

                // the object has no active representation.
                push_shared_ptr_userdata(l, v);
                auto sharedPtr = clg::ref::from_stack(l);

                clg::ref luaRepresentation = clg::ref::from_cpp(l, clg::table{});

                luaRepresentation.set_metatable(clg::table{
                        {"__newindex", clg::ref::from_cpp(l, clg::cfunction<handle_virtual_func>("<handle virtual proc setter>"))},
                        {"__gc", clg::ref::from_cpp(l, lua_CFunction(handle_gc)) },
                        {"__index", sharedPtr},
                });
                v->mLuaRepresentation = luaRepresentation;
                luaRepresentation.push_value_to_stack(l);
                return 1;
            }
            return 1;
        }
    };

    /**
     * userdata
     */
    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr: public converter_shared_ptr_impl<T> {};

    /**
     * userdata
     */
    template<typename T>
    struct converter<std::shared_ptr<T>>: converter_shared_ptr<T> {};
}
