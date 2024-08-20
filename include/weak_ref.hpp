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

        void emplace(ref r, lua_State* L = clg::state()) {
            clg::stack_integrity_check check(L);
            mWrapperObject = clg::ref::from_cpp(L, clg::table{
                {"value", std::move(r) },
            });
            mWrapperObject.set_metatable(clg::table{
                    { "__mode", clg::ref::from_cpp(L, "v") }, // weak reference mode for mWrapperObject's fields
            });
        }

        clg::ref lock() const noexcept {
            if (mWrapperObject.isNull()) {
                return nullptr;
            }
            return mWrapperObject["value"].ref();
        }
        const clg::table_view& lua_weak() {
            return mWrapperObject;
        }

    private:
        clg::table_view mWrapperObject;
    };
}