//
// Created by alex2 on 04.05.2021.
//

#pragma once

#include "converter.hpp"
#include "ref.hpp"
#include <vector>

namespace clg {

    /**
     * Тип возвращаемого значения функцией Lua для тех случаев, когда фукнция может возвращать разное количество и тип
     * значений.
     * \example
     * <code>
     * clg::dynamic_result r = vm["my_func"].call&lt;clg::dynamic_result&gt;();
     * </code>
     *
     */
    class dynamic_result {
    private:
        lua_State* mState;
        std::vector<clg::ref> mData;

        void push_value_to_stack(int index) const noexcept {
            mData[index].push_value_to_stack();
        }
        dynamic_result() = default;
    public:
        static converter_result<dynamic_result> from_lua(lua_State* state) {
            std::size_t s = lua_gettop(state);
            dynamic_result result;
            result.mState = state;
            result.mData.reserve(s);

            for (auto i = 0; i < s; ++i) {
                auto r = clg::get_from_lua_raw<clg::ref>(state, i + 1);
                if (r.is_error()) {
                    return r.error();
                }
                result.mData.push_back(std::move(*r));
            }
            lua_pop(result.mState, s);
        }

        ~dynamic_result() {
        }

        template<typename T>
        [[nodiscard]] T get(int index) const {
            stack_integrity_check stack(mState);
            push_value_to_stack(index);
            auto p = converter<std::decay_t<T>>::from_lua(mState, -1);
            lua_pop(mState, 1);
            return p;
        }

        [[nodiscard]] bool is_nil(int index) const {
            push_value_to_stack(index);
            bool b = lua_isnil(mState, -1);
            lua_pop(mState, 1);
            return b;
        }

        [[nodiscard]] size_t size() const {
            return mData.size();
        }
    };


    template<>
    struct converter<dynamic_result> {
        static converter_result<dynamic_result> from_lua(lua_State* l, int n) {
            return dynamic_result::from_lua(l);
        }
    };
}