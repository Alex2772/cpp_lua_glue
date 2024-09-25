#pragma once

#include <memory>
#include <optional>
#include <set>
#include "lua.hpp"
#include "weak_ref.hpp"
#include "table.hpp"
#include "userdata_view.hpp"


namespace clg {
    class lua_self;
    namespace impl {
        inline void invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value);
        inline void update_strong_userdata(clg::lua_self& self, clg::ref value);

        inline void push_to_userdata_ephemeron(lua_State* l, int index) {
            index = lua_absindex(l, index);
            lua_pushstring(l, "userdata_ephemeron");
            lua_rawget(l, LUA_REGISTRYINDEX);
            lua_pushvalue(l, index);
            lua_pushboolean(l, true);
            lua_rawset(l, -3);
            lua_pop(l, 1);
        }
    }

    namespace debug {
        inline std::function<void(const clg::ref&)>& on_object_created() {
            static std::function<void(const clg::ref&)> v;
            return v;
        };
    }

    [[nodiscard]]
    std::set<lua_self*>& object_counters();

    /**
     * @brief When extended from, allows to avoid extra overhead when passed to lua. Also allows lua code to use the
     * object as a table.
     * @details
     * @code{cpp}
     * class Person: clg::lua_self {
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
        friend void impl::update_strong_userdata(clg::lua_self& self, clg::ref value);
        template<typename T, typename EnableIf>
        friend struct converter_shared_ptr_impl_to;
    public:

        clg::table_view luaDataHolder() const noexcept {
            auto userdata = luaSelf();
            if (userdata.isNull()) {
                return {};
            }
            auto res = userdata.uservalue();
            assert(("uservalue must exist after object has been initialized", !res.isNull()));
            return res;
        }

        virtual ~lua_self() = default;

        size_t clg_use_count() const {
            return mUseCount.use_count();
        }

    protected:

        clg::userdata_view luaSelf() const noexcept {
            if (!mInitialized) {
                return {}; // userdata do not exist yet
            }
            if (!mStrongUserdata.isNull()) {
                return mStrongUserdata;
            }
            auto res = mWeakUserdata.lock();
            assert(("lua self userdata must exist after its initialization", !res.isNull()));
            return res;
        }

        virtual void handle_lua_virtual_func_assignment(std::string_view name, clg::ref value) {}

    private:

        clg::userdata_view mStrongUserdata;
        /**
         * @brief Weak reference to lua userdata pushed to lua
         */
        clg::ephemeron_weak_ref mWeakUserdata;

        std::weak_ptr<void> mUseCount;

        bool mInitialized = false;

#if CLG_OBJECT_COUNTER
        struct ObjectCounter {
            ObjectCounter(lua_self* s): s(s) {
                object_counters().insert(s);

            }
            ~ObjectCounter() {
                object_counters().erase(s);
            }

        private:
            lua_self* s;
        };
        std::optional<ObjectCounter> mObjectCounter;
#endif
    };
    inline void impl::invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value) {
        s.handle_lua_virtual_func_assignment(name, std::move(value));
    }

    inline void impl::update_strong_userdata(clg::lua_self& self, clg::ref value) {
        assert(!value.isNull());
        assert(self.mStrongUserdata.isNull());
        self.mStrongUserdata = std::move(value);
    }


    /**
    * @brief Forces changing clg userdata state to registry state
    * @note clg at the moment can't handle registry links to clg userdata properly, it may lead to memory links,
    *		this function helps to resolve these links (e.g. using custom garbage collector cycle).
    *		You should force switching to registry state every userdata that is not reachable in regular lua usage.
    */
    inline void forceSwitchToRegistryState(clg_userdata_view userdata) {
        auto helper = userdata.get_userdata_helper();
        if (!helper->is_strong_ptr_stored()) {
            return;
        }
        auto self = helper->as_lua_self();
        if (!self) {
            return;
        }
        impl::update_strong_userdata(*self, std::move(userdata));
        auto b = helper->switch_to_weak();
        assert(b);
    }

    /**
     * userdata
     */
    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr_impl_from {
        static converter_result<std::shared_ptr<T>> from_lua(lua_State* l, int n) {
            if (lua_isnil(l, n)) {
                return std::shared_ptr<T>(nullptr);
            }

            if (!impl::is_clg_userdata(l, n)) {
                return clg::converter_error{"not a clg userdata"};
            }

            return static_cast<userdata_helper*>(lua_touserdata(l, n))->as<T>();
        }
    };

    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr_impl_to {
        static void push_new_userdata(lua_State* l, const std::shared_ptr<T>& v) {
            clg::stack_integrity_check c(l, 1);
            auto userdata = static_cast<userdata_helper*>(lua_newuserdata(l, sizeof(userdata_helper)));
            impl::push_to_userdata_ephemeron(l, -1);
            new(userdata) userdata_helper(v);
            if constexpr (std::is_polymorphic_v<T>) {
                if (auto self = std::dynamic_pointer_cast<clg::lua_self>(v)) {
                    assert(("lua self userdata should be initialized only once", !self->mInitialized));
                    self->mUseCount = v;
                    lua_pushvalue(l, -1);       // duplicate userdata
                    self->mWeakUserdata.emplace(ref::from_stack(l));   // saving user data in a weak reference
                    assert(!self->mWeakUserdata.lock().isNull());
                    lua_createtable(l, 0, 0);   // create empty table
                    lua_setuservalue(l, -2);    // setting uservalue (clg stores table with arbitrary lua data in uservalue slot)
                    self->mInitialized = true;
                }
            }

            clg::state_interface s(l);
            auto clazz = s.global_variable(clg::class_name<T>());
            assert(!clazz.isNull());
            auto clazzMeta = clazz.metatable();
            assert(!clazzMeta.isNull());
            auto meta = clazzMeta.template as<table_view>()["__clg_metatable"].ref();
            assert(!meta.isNull());
            meta.push_value_to_stack(l);
            assert(lua_type(l, -1) == LUA_TTABLE);
            lua_setmetatable(l, -2);
        }

        static int to_lua(lua_State* l, std::shared_ptr<T> v) {
            stack_integrity_check check(l, 1);
            if (v == nullptr) {
                lua_pushnil(l);
                return 1;
            }

            if constexpr (std::is_polymorphic_v<T>) {
                if (auto self = dynamic_cast<clg::lua_self*>(v.get())) {
#if CLG_OBJECT_COUNTERd_v
                    if (!self->mObjectCounter) {
                        self->mObjectCounter.emplace(self);
                    }
#endif

                    if (!self->mInitialized) {
                        // userdata is not created yet, so create it and return
                        push_new_userdata(l, v);
                        return 1;
                    }

                    if (!self->mStrongUserdata.isNull()) {
                        // in this case, at the moment userdata is unreachable in regular lua usage and stores only in lua registry
                        self->mStrongUserdata.push_value_to_stack(l);
                        impl::push_to_userdata_ephemeron(l, -1);
                        auto helper = static_cast<userdata_helper*>(lua_touserdata(l, -1));
                        assert(helper != nullptr);
                        auto b = helper->switch_to_shared();
                        assert(b);
                        self->mWeakUserdata.emplace(self->mStrongUserdata, l);
                        self->mStrongUserdata = clg::ref(nullptr);
                        assert(!self->mWeakUserdata.lock().isNull());
                        return 1;
                    }

                    // otherwise, we have already userdata in lua, push it using weak reference
                    self->luaSelf().push_value_to_stack(l);
                    return 1;
                }
            }

            // if we get there, we don't use lua self and we have to create new userdata each time
            push_new_userdata(l, v);
            return 1;
        }
    };

    /**
     * userdata
     */
    template<typename T, typename EnableIf = void>
    struct converter_shared_ptr : converter_shared_ptr_impl_from<T>,
                                  converter_shared_ptr_impl_to<T, std::enable_if_t<!std::is_void_v<T>>> {};

    template<typename T>
    struct converter_shared_ptr<T, std::enable_if_t<std::is_void_v<T>>> : converter_shared_ptr_impl_from<T> {};

    /**
     * userdata
     */
    template<typename T>
    struct converter<std::shared_ptr<T>>: converter_shared_ptr<T> {};
}
