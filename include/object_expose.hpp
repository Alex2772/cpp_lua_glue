#pragma once

#include "weak_ref.hpp"
#include "table.hpp"
#include <cstring>

namespace clg {
    class lua_self;
    namespace impl {
        inline void invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value);
    }

    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr_lua_self_impl;

    template<typename T, typename... Args>
    inline std::shared_ptr<T> make_shared(Args&&... args);

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

        template<typename T, typename EnableIf>
        friend struct clg::converter_shared_ptr_lua_self_impl;

        template<typename T, typename... Args>
        friend inline std::shared_ptr<T> make_shared(Args&&... args);

        friend struct shared_ptr_helper_lua_self;
        friend struct shared_ptr_helper_lua_self_weak;
    public:
        lua_self() {
            printf("lua_self() %p\n", this);
        }
        virtual ~lua_self() = default; // no way, this should be polymorphic anyway

        table_view luaDataHolder() const noexcept {
            return static_cast<table_view>(mWeakPtrAndUserdataHolder.lock()); // avoid copy
        }

    protected:

        clg::ref luaSelf() const noexcept {
            return mObjectHolderSharedCppLua.lock();
        }

        virtual void handle_lua_virtual_func_assignment(std::string_view name, clg::ref value) {}

        /**
         * @brief Returns shared_ptr even if weak_ptr.lock() == nullptr
         * @details
         * Expects this to be valid.
         *
         * Used for obtaining a c++ shared_ptr if the object is hold by lua only.
         */
        std::shared_ptr<lua_self> reincarnate_shared_ptr_if_needed() {
            auto& self = lua->ptr;
            auto sharedPtr = self.lock();
            if (sharedPtr) {
                return sharedPtr;
            }
            printf("shared_ptr reincarnation! lua_self = %p\n", this);
            // the lua land shared_ptr is dead, recreate it again (reincarnation)
            struct control_block {
                char padding[8];
                int32_t strongs; // we could have made them atomic, but no thanks, wasted cycles on atomic operations
                int32_t weaks;   //
            };
            struct weak_ptr_internals {
                void* obj;
                control_block* control;
            };

            auto& hack = reinterpret_cast<const weak_ptr_internals&>(self);

            // compare the hacked fields with public data so we are hacking correctly
            assert(hack.obj == this);
            assert(hack.control->strongs == 0);

            hack.control->strongs++;
            // because one weak belongs to all shared_ptr's; as we don't have any shared_ptrs, incrementing this as
            // well. We will decrement strongs later but not weaks
            hack.control->weaks++;

            assert(!self.expired());
            sharedPtr = self.lock();
            auto strongUserdata = mWeakPtrAndUserdataHolder.lock();
            assert(!strongUserdata.isNull()); // if object comes from lua, lua should have kept userdata for us
            cpp.emplace(std::move(strongUserdata));
            hack.control->strongs--;
            assert(sharedPtr != nullptr);
            assert(sharedPtr.use_count() == 1);
            assert(hack.control->strongs == 1);
            return sharedPtr;
        }

    private:
        /**
         * @brief Actual userdata table storage.
         */
        clg::weak_ref mWeakPtrAndUserdataHolder; // weakly holds lua_self::cpp_land::userdata

        /**
         * @brief Actual table that is being pushed to lua to represent the object.
         * @details
         * Weak reference to the interface table that is exact value is being pushed to lua to represent this object.
         * The reference may die and reincarnate multiple times during the lifetime of the c++ object.
         */
        clg::weak_ref mObjectHolderSharedCppLua;

        struct cpp_land {
            clg::ref userdata; // strong ref to userdata
        };

        /**
         * @brief If valid, C++ part holds a std::shared_ptr to the object.
         */
        std::optional<cpp_land> cpp;

        struct lua_land {
            std::weak_ptr<lua_self> ptr;
        };

        /**
         * @brief If valid, Lua part holds a noncollected reference to the object.
         */
        std::optional<lua_land> lua;

        void destroyIfBothCppAndLuaHasNoRefs() {
            if (!cpp && !lua) {
                printf("destroying object! lua_self = %p\n", this);
                delete this;
            }
        }

        template<typename T>
        std::shared_ptr<T> make_shared_impl() {
            return make_shared_impl(dynamic_cast<T*>(this));
        }

        template<typename T>
        std::shared_ptr<T> make_shared_impl(T* self) {
            return std::shared_ptr<T>(self, [](T* obj) {
                lua_self* downcasted = obj;
                printf("~shared_ptr() %p; lua_self = %p\n", obj, downcasted);
                // careful! if lua calls gc between reset and destroyIfBothCppAndLuaHasNoRefs, we will get double free
                // unless we have extended the shared_ptr's lifetime if any
                std::shared_ptr<clg::lua_self> extendSharedPtrLifetime;
                if (obj->lua) {
                    extendSharedPtrLifetime = obj->lua->ptr.lock();
                }

                obj->cpp.reset();
                obj->destroyIfBothCppAndLuaHasNoRefs();
            });
        }

    };
    inline void impl::invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value) {
        s.handle_lua_virtual_func_assignment(name, std::move(value));
    }


    /**
     * userdata
     */
    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr_impl {
        static converter_result<std::shared_ptr<T>> from_lua(lua_State* l, int n) {
            if (lua_isnil(l, n)) {
                return std::shared_ptr<T>(nullptr);
            }

            if (lua_isuserdata(l, n)) {
                return reinterpret_cast<shared_ptr_helper*>(lua_touserdata(l, n))->as<T>();
            }
            return clg::converter_error{"not a userdata"};
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

        static void push_strong_ref_holder_object(lua_State* l, std::shared_ptr<T> v, clg::ref dataHolderRef) {
            push_shared_ptr_userdata(l, std::move(v));
            clg::push_to_lua(l, clg::table{{"clg_strongref", clg::ref::from_stack(l)}});
            clg::push_to_lua(l, clg::table{
                { "__index", dataHolderRef },
                { "__newindex", std::move(dataHolderRef) },
            });
            lua_setmetatable(l, -2);
        }

        static int to_lua(lua_State* l, std::shared_ptr<T> v) {
            if (v == nullptr) {
                lua_pushnil(l);
                return 1;
            }

            push_shared_ptr_userdata(l, std::move(v));

            return 1;
        }
    };

    struct shared_ptr_helper_lua_self_weak: impl::dealloc_helper {
        lua_self* ptr;
        const std::type_info& type;
        bool valid = true;


        template<typename T>
        shared_ptr_helper_lua_self_weak(T* ptr):
            ptr(ptr),
            type(typeid(T))
        {
        }
        ~shared_ptr_helper_lua_self_weak() override {
            assert(valid);
            valid = false;
        }

        template<typename T>
        clg::converter_result<std::shared_ptr<T>> as() const {
            {
                // const auto* expected = typeid(shared_ptr_helper_lua_self_weak).name();
                // const auto* actual = typeid(*this).name();
                // assert(strcmp(expected, actual) == 0);
            }
            static int counter = 0;
            ++counter;
            static_assert(std::is_base_of_v<clg::lua_self, T>);
            assert(valid == true);
            auto sharedPtr = ptr->reincarnate_shared_ptr_if_needed();
            assert(valid == true);
            if constexpr (std::is_base_of_v<allow_lua_inheritance, T>) {
                return std::dynamic_pointer_cast<T>(std::move(sharedPtr));
            } else {
                if (auto& expected = typeid(T); expected != type) {
                    static std::string e = std::string("type mismatch: expected ") + expected.name() + "\nnote: extend clg::allow_lua_inheritance to allow inheritance";
                    return converter_error{e.c_str()};
                }
                return std::dynamic_pointer_cast<T>(std::move(sharedPtr));
            }
        }
    };

    struct shared_ptr_helper_lua_self: shared_ptr_helper_lua_self_weak {

        template<typename T>
        shared_ptr_helper_lua_self(T* ptr): shared_ptr_helper_lua_self_weak(ptr)
        {
            printf("shared_ptr_helper_lua_self() = %p, lua_self = %p\n\n", this, ptr);
        }
        ~shared_ptr_helper_lua_self() override {
            printf("~shared_ptr_helper_lua_self() = %p, lua_self = %p\n\n", this, ptr);
            assert(ptr->lua);
            auto extendSharedPtrLifetime = ptr->lua->ptr.lock();
            ptr->lua.reset();
            if (extendSharedPtrLifetime) {
                // calls destroyIfBothCppAndLuaHasNoRefs() if cpp has no refs remaining as well
                extendSharedPtrLifetime = nullptr;
            } else {
                ptr->destroyIfBothCppAndLuaHasNoRefs();
            }
        }
    };

    template<typename T, typename EnableIf>
    struct converter_shared_ptr_lua_self_impl {
        static_assert(std::is_base_of_v<clg::lua_self, T>);

        static converter_result<std::shared_ptr<T>> from_lua(lua_State* l, int n) {
            if (lua_isnil(l, n)) {
                return std::shared_ptr<T>(nullptr);
            }

            if (lua_istable(l, n)) {
                lua_getfield(l, n, "clg_strongref_ls");
                if (!lua_isuserdata(l, -1)) {
                    lua_pop(l, 1);
                    return clg::converter_error{"not a cpp object (clg::lua_self)"};
                }
                auto helper = reinterpret_cast<shared_ptr_helper_lua_self*>(lua_touserdata(l, -1));
                auto p = helper->as<T>();
                lua_pop(l, 1);
                return p;
            }
            return std::shared_ptr<T>(nullptr);
        }

        static void push_shared_ptr_userdata(lua_State* l, std::shared_ptr<T> v) {
            clg::stack_integrity_check c(l, 1);
            auto classname = clg::class_name<T>();
            auto t = reinterpret_cast<shared_ptr_helper_lua_self*>(lua_newuserdata(l, sizeof(shared_ptr_helper_lua_self)));
            new(t) shared_ptr_helper_lua_self(v.get());

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
            auto t = reinterpret_cast<shared_ptr_helper_lua_self_weak*>(lua_newuserdata(l, sizeof(shared_ptr_helper_lua_self_weak)));
            new(t) shared_ptr_helper_lua_self_weak(v.lock().get());

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
            clg::push_to_lua(l, clg::table{{"clg_strongref_ls", clg::ref::from_stack(l)}});
            clg::push_to_lua(l, clg::table{
                { "__index", dataHolderRef },
                { "__newindex", std::move(dataHolderRef) },
            });
            lua_setmetatable(l, -2);
        }

        static void handle_virtual_func(clg::table_view table, std::string_view key, clg::ref value) {
            clg::table_view(clg::table_view(table.metatable())["__index"].ref())[key] = value;
            const auto L = table.lua();
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

                auto r = reinterpret_cast<shared_ptr_helper_lua_self_weak*>(lua_touserdata(L, -1))->as<T>();
                if (r.is_error()) {
                    throw std::runtime_error("failed to convert from shared_ptr_helper to as<T>");
                }
                auto& object = *r;

                impl::invoke_handle_lua_virtual_func_assignment(*(object.get()), key, std::move(value));
                lua_pop(L, 1);
            } catch (...) {}
        };

        static int to_lua(lua_State* l, const std::shared_ptr<T>& v) {
            if (v == nullptr) {
                lua_pushnil(l);
                return 1;
            }

            if (!v->lua) {
                v->lua.emplace(v);
            }

            auto& weakRef = v->mObjectHolderSharedCppLua;
            if (auto lock = weakRef.lock()) {
                lock.push_value_to_stack(l);
                return 1;
            }

            // if (!v->cpp_land) {
            //     v->cpp_land.emplace();
            // }

            // if object comes from cpp, cppland data should be alive
            assert(v->cpp.has_value());
            auto& dataHolder = v->cpp->userdata;
            if (dataHolder.isNull()) {
                // should compose strong ref holder and weak ref holder objects
                // weak ref and data holder object
                lua_createtable(l, 0, 0);
                push_weak_ptr_userdata(l, v);

                auto r = clg::ref::from_cpp(l, clg::table{});
                r.set_metatable(clg::table{
                    { "__index", clg::ref::from_stack(l) },
                });


                clg::push_to_lua(l, clg::table{
                        {"__index",    std::move(r)},
                        {"__newindex", clg::ref::from_cpp(l, clg::cfunction<handle_virtual_func>("<handle virtual proc setter>"))},
                });
                lua_setmetatable(l, -2);

                v->mWeakPtrAndUserdataHolder = dataHolder = clg::ref::from_stack(l);
            }

            push_strong_ref_holder_object(l, std::move(v), dataHolder);
            lua_pushvalue(l, -1);
            weakRef = clg::weak_ref(clg::ref::from_stack(l));
            return 1;
        }
    };

    /**
     * lua userdata
     */
    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr2: public converter_shared_ptr_impl<T> {};

    /**
     * lua_self
     */
    template<typename T>
    struct converter_shared_ptr2<T, std::enable_if_t<std::is_base_of_v<clg::lua_self, T>>>: public converter_shared_ptr_lua_self_impl<T> {};

    /**
     * can be partially specifilized by user with std::enable_if_t
     */
    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr: public converter_shared_ptr2<T> {};


    template<typename T>
    struct converter<std::shared_ptr<T>>: converter_shared_ptr<T> {};

    template<typename T>
    concept is_lua_self_object = std::is_base_of_v<clg::lua_self, T>;
}

template<typename T, typename... Args>
inline std::shared_ptr<T> clg::make_shared(Args&&... args) {
    auto obj = new T(std::forward<Args>(args)...);
    try {
        obj->cpp.emplace();
        return obj->make_shared_impl(obj);
    } catch (...) {
        delete obj;
        throw;
    }
}

namespace std {
    template<typename T, typename... Args>
    requires clg::is_lua_self_object<T>
    std::shared_ptr<T> make_shared(Args&&... args) {
        static_assert(!std::is_base_of_v<clg::lua_self, T>, "TRAP: regular std::make_shared for lua_self objects is \n"
            "forbidden. You should never use std::make_shared for clg::lua_self objects. clg::lua_self has complex\n"
            "memory management scheme that incomporates C++'s shared_ptr and Lua's garbage collector. Use \n"
            "clg::make_shared instead.");
        return nullptr;
    }

    template<typename T, typename... Args>
    requires clg::is_lua_self_object<T>
    std::unique_ptr<T> make_unique(Args&&... args) {
        static_assert(!std::is_base_of_v<clg::lua_self, T>, "TRAP: std:make_unique for lua_self objects is \n"
            "forbidden. You should never use std::make_unique for clg::lua_self objects. clg::lua_self has complex\n"
            "memory management scheme that incomporates C++'s shared_ptr and Lua's garbage collector. Use \n"
            "clg::make_shared instead.");
        return nullptr;
    }
}