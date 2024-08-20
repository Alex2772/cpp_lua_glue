#pragma once

#include "table.hpp"
#include "ref.hpp"

namespace clg {
    class weak_ref {
    public:
        weak_ref() = default;

        weak_ref(const ref& r) {
            emplace(r);
        }

        void emplace(const ref& r, lua_State* l = clg::state()) {
            clg::stack_integrity_check check(l);
            if (mWrapperObject.isNull()) {
                mWrapperObject = clg::ref::from_cpp(l, clg::table{
                    {"value", nullptr},
                });
            }
            mWrapperObject.raw_set("value", r);
        }

        clg::ref lock() const noexcept {
            if (mWrapperObject.isNull()) {
                return nullptr;
            }
            return mWrapperObject["value"].ref();
        }

    private:
        clg::table_view mWrapperObject;
    };
};