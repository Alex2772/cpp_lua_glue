#pragma once

#include <memory>
#include <optional>
#include <set>
#include "lua.hpp"
#include "weak_ref.hpp"
#include "table.hpp"


namespace clg {
    class lua_self;
    namespace impl {
        inline void invoke_handle_lua_virtual_func_assignment(clg::lua_self& s, std::string_view name, clg::ref value);
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
        friend struct converter_shared_ptr_impl;
    public:

        table_view luaDataHolder() const noexcept {
            if (!mDataHolder.isNull()) {
                return mDataHolder;
            }

            auto userdata = mUserdataHolder.lock().as<userdata_view>();
            assert(!userdata.isNull());
            return userdata.uservalue();
        }

        virtual ~lua_self() = default;

        size_t clg_use_count() const {
            return mUseCount.use_count();
        }

        void updateDataholder(clg::table_view t) noexcept {
            mDataHolder = std::move(t);
            assert(!mDataHolder.isNull());
            assert(mDataHolder.as_converter_result<table>().is_ok()); // temporary
        }

    protected:

        clg::ref luaSelf() const noexcept {
            return mUserdataHolder.lock();
        }

        virtual void handle_lua_virtual_func_assignment(std::string_view name, clg::ref value) {}

    private:
        /**
         * @brief Lua table with some arbirtary data accessible from lua
         * @note If there is at least one reference on the object's userdata in lua, this table is stored in uservalue
         *       field of userdata (clg uses only 1 uservalue slot) and mDataHolder is nil, use luaDataHolder() method
         *       to access it. (We need it to avoid cyclic links and help lua garbage collector to work properly)
         *       If there is no userdata in lua (userdata has been collected), mDataHolder must be non-nil and contain
         *       data holder table.
         */
        clg::table_view mDataHolder;
        /**
         * @brief Weak reference to lua userdata pushed to lua
         */
        clg::weak_ref mUserdataHolder;
        /**
         * @brief True if we need to initialized data holder.
         * @note Data holder table will be created on the first time pushing to lua
         */
        bool mIsDataHolderInitialized = false;

        std::weak_ptr<void> mUseCount;

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

            if (lua_isuserdata(l, n)) {
                return reinterpret_cast<shared_ptr_helper*>(lua_touserdata(l, n))->as<T>();
            }

            return clg::converter_error{"not a userdata"};
        }

        static void push_shared_ptr_userdata(lua_State* l, std::shared_ptr<T> v) {
            clg::stack_integrity_check c(l, 1);
            auto classname = clg::class_name<T>();
            auto t = static_cast<shared_ptr_helper*>(lua_newuserdata(l, sizeof(shared_ptr_helper)));
            if constexpr (std::is_polymorphic_v<T>) {
                if (auto self = dynamic_cast<clg::lua_self*>(v.get())) {
                    self->mUseCount = v;
                    if (!self->mIsDataHolderInitialized) {
                        lua_createtable(l, 0, 0);
                        lua_setuservalue(l, -2);
                        self->mIsDataHolderInitialized = true;
                    }
                    else {
                        assert(!self->mDataHolder.isNull());
                        self->mDataHolder.push_value_to_stack(l);
                        lua_setuservalue(l, -2);
                        self->mDataHolder = nullptr;
                    }
                }
            }

            new(t) shared_ptr_helper(std::move(v));
#if LUA_VERSION_NUM != 501
            auto r = lua_getglobal(l, classname.c_str());
#else
            lua_getglobal(l, classname.c_str());
            auto r = lua_type(l, -1);
#endif
            if (r != LUA_TNIL) {
                if (lua_getmetatable(l, -1)) {
                    lua_setmetatable(l, -3);
                }
                lua_pop(l, 1);
            }
        }

        static int to_lua(lua_State* l, std::shared_ptr<T> v) {
            stack_integrity_check check(l, 1);
            if (v == nullptr) {
                lua_pushnil(l);
                return 1;
            }

            if constexpr (std::is_polymorphic_v<T>) {
                if (auto self = dynamic_cast<clg::lua_self*>(v.get())) {
                    auto& weakRef = self->mUserdataHolder;
                    if (auto lock = weakRef.lock()) {
                        lock.push_value_to_stack(l);
                        return 1;
                    }
#if CLG_OBJECT_COUNTER
                    if (!self->mObjectCounter) {
                        self->mObjectCounter.emplace(self);
                    }
#endif
                }
            }

            push_shared_ptr_userdata(l, std::move(v));
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
