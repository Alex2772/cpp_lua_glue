#pragma once

#include "weak_ref.hpp"
#include "table.hpp"

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
        friend void impl::invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value);
    public:

        const table_view& luaDataHolder() const noexcept {
            return static_cast<const table_view&>(mWeakPtrAndDataHolder); // avoid copy
        }


    protected:

        clg::ref luaSelf() const noexcept {
            return mSharedPtrHolder.lock();
        }

        virtual void handle_lua_virtual_func_assignment(std::string_view name, clg::ref value) {}

    private:
        friend clg::ref& lua_self_weak_ptr_and_data_holder(lua_self& s);
        friend clg::weak_ref& lua_self_shared_ptr_holder(lua_self& s);
        clg::ref      mWeakPtrAndDataHolder;
        clg::weak_ref mSharedPtrHolder;

    };
    inline void impl::invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value) {
        s.handle_lua_virtual_func_assignment(name, std::move(value));
    }

    inline clg::ref& lua_self_weak_ptr_and_data_holder(lua_self& s) {
        return s.mWeakPtrAndDataHolder;
    }
    inline clg::weak_ref& lua_self_shared_ptr_holder(lua_self& s) {
        return s.mSharedPtrHolder;
    }


    /**
     * userdata
     */
    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr_impl {
        static constexpr bool use_lua_self = std::is_base_of_v<clg::lua_self, T>;

        static converter_result<std::shared_ptr<T>> from_lua(lua_State* l, int n) {
            if (lua_isnil(l, n)) {
                return std::shared_ptr<T>(nullptr);
            }

            if constexpr(use_lua_self) {
                if (lua_istable(l, n)) {
                    lua_getfield(l, n, "clg_strongref");
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
            const auto L = table.lua();
            if constexpr (use_lua_self) {
                try {
                    clg::stack_integrity_check check(L);
                    if (!value.isFunction()) {
                        return;
                    }

                    clg::table_view meta = table.metatable();
                    if (meta.isNull()) {
                        return;
                    }

                    auto index1 = meta["__index"].ref();
                    if (index1.isNull()) {
                        return;
                    }

                    clg::table_view index1Metatable = index1.metatable();
                    if (index1Metatable.isNull()) {
                        return;
                    }

                    auto index2 = index1Metatable["__index"].ref();
                    if (index2.isNull()) {
                        return;
                    }
                    index2.push_value_to_stack();

                    if (!lua_isuserdata(L, -1)) {
                        lua_pop(L, 1);
                        return;
                    }

                    auto r = reinterpret_cast<shared_ptr_helper*>(lua_touserdata(L, -1))->as<T>();
                    if (r.is_error()) {
                        throw std::runtime_error("failed to convert from shared_ptr_helper to as<T>");
                    }
                    auto& object = *r;

                    impl::invoke_handle_lua_virtual_func_assignment(*(object.get()), key, std::move(value));
                    lua_pop(L, 1);
                } catch (...) {}
            }
        };

        static int to_lua(lua_State* l, std::shared_ptr<T> v) {
            if (v == nullptr) {
                lua_pushnil(l);
                return 1;
            }

            if constexpr(!use_lua_self) {
                push_shared_ptr_userdata(l, std::move(v));
            } else {
                auto& weakRef = lua_self_shared_ptr_holder(*v);
                if (auto lock = weakRef.lock()) {
                    lock.push_value_to_stack();
                    return 1;
                }

                auto& dataHolder = lua_self_weak_ptr_and_data_holder(*v);
                if (dataHolder.isNull()) {
                    // should compose strong ref holder and weak ref holder objects
                    // weak ref and data holder object
                    lua_createtable(l, 0, 0);
                    push_weak_ptr_userdata(l, v);

                    if constexpr (use_lua_self) {
                        auto r = clg::ref::from_cpp(l, clg::table{});
                        r.set_metatable(clg::table{
                               { "__index", clg::ref::from_stack(l) },
                        });


                        clg::push_to_lua(l, clg::table{
                                {"__index",    std::move(r)},
                                {"__newindex", clg::ref::from_cpp(l, clg::cfunction<handle_virtual_func>())},
                        });
                    } else {
                        clg::push_to_lua(l, clg::table{
                                {"__index",    clg::ref::from_stack(l)},
                        });
                    }
                    lua_setmetatable(l, -2);

                    dataHolder = clg::ref::from_stack(l);
                }

                push_strong_ref_holder_object(l, std::move(v), dataHolder);
                lua_pushvalue(l, -1);
                weakRef = clg::weak_ref(clg::ref::from_stack(l));
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
