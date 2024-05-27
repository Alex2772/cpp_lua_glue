#pragma once

#include "table.hpp"
#include "ref.hpp"

namespace clg {
    class weak_ref {
    public:
        weak_ref(ref r) {
            emplace(std::move(r));
        }

        weak_ref() {

        }

        void emplace(ref r) {
            auto s = r.lua();
            emplace(std::move(r), s);
        }

        void emplace(ref r, lua_State* L /* = r.lua() */) {
            mWrapperObject = clg::ref(clg::ref::from_cpp(L, clg::table{
                {"value", r },
            }));
            clg::stack_integrity_check check(L);
            mWrapperObject.push_value_to_stack(L);

            clg::push_to_lua(L, clg::table{
                { "__mode", clg::ref::from_cpp(L, "v") }, // weak reference mode for mWrapperObject's fields
            });

            lua_setmetatable(L, -2);
            lua_pop(L, 1);
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