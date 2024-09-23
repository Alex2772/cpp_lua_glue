#pragma once

#include "table.hpp"
#include "table_view.hpp"

namespace clg {
    class weak_ref {
    public:
        weak_ref() = default;

        explicit weak_ref(const ref& r) {
            emplace(r);
        }

        void emplace(const ref& r, lua_State* l = clg::state()) {
            clg::stack_integrity_check check(l);
            if (mWrapperObject.isNull()) {
                mWrapperObject.set_metatable(clg::table{
                    { "__mode", clg::ref::from_cpp(l, "v") }, // weak reference mode for mWrapperObject's values
                });
                mWrapperObject.set_metatable(clg::table{
                        {"__mode", clg::ref::from_cpp(l, "v")} // weak reference mode for mWrapperObject's values
                });
            }
            mWrapperObject.raw_set("value", r);
        }

        clg::ref lock() const noexcept {
            if (mWrapperObject.isNull()) {
                return clg::ref(nullptr);
            }
            return mWrapperObject["value"].ref();
        }

    private:
        clg::table_view mWrapperObject;
    };


    /**
     * @brief Unlike a weak_ref, ephemeron_weak_ref store weak value in a table with weak keys.
     * @note Use it instead of regular weak_ref if you need to make weak ref accessible between marking for finalization
     *       and invoking __gc metamethod (details of lua garbage collector)
     */
    class ephemeron_weak_ref {
    public:
        ephemeron_weak_ref() = default;

        explicit ephemeron_weak_ref(const ref& r) {
            emplace(r);
        }

        void emplace(const ref& r, lua_State* l = clg::state()) {
            clg::stack_integrity_check check(l);
            mWrapperObject = clg::ref::from_cpp(l, clg::table{});
            mWrapperObject.set_metatable(clg::table{ {"__mode", clg::ref::from_cpp(l, "k")}, });
            mWrapperObject.raw_set(r, true);
        }

        clg::ref lock(lua_State* l = clg::state()) const noexcept {
            stack_integrity_check c(l);
            if (mWrapperObject.isNull()) {
                return clg::ref(nullptr);
            }

            mWrapperObject.push_value_to_stack(l);
            lua_pushnil(l);
            while (lua_next(l, -2) != 0) {
                lua_pushvalue(l, -2);
                auto stored_ref = clg::get_from_lua<clg::ref>(l, -1);
                assert(clg::get_from_lua<bool>(l, -2) == true);
                lua_pop(l, 4);
                return stored_ref;
            }

            return clg::ref(nullptr);
        }

    private:
        clg::table_view mWrapperObject;
    };
};