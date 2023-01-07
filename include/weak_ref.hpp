#pragma once

#include "table.hpp"
#include "ref.hpp"

namespace clg {
    class weak_ref {
    public:
        weak_ref(ref r): mWrapperObject(clg::ref::from_cpp(r.lua(), clg::table{
            {"value", r },
        })) {
            const auto L = mWrapperObject.lua();
            clg::stack_integrity_check check(L);
            mWrapperObject.push_value_to_stack();

            clg::push_to_lua(L, clg::table{
                { "__mode", clg::ref::from_cpp(L, "v") }, // weak reference mode for mWrapperObject's fields
            });

            lua_setmetatable(L, -2);
            lua_pop(L, 1);
        }

        weak_ref() {

        }

        clg::ref lock() const noexcept {
            if (mWrapperObject.isNull()) {
                return nullptr;
            }
            return mWrapperObject.as<clg::table>()["value"];
        }
        clg::ref lua_weak() {
            return mWrapperObject;
        }

    private:
        clg::ref mWrapperObject;
    };
}