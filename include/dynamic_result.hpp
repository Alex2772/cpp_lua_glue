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
    public:
        dynamic_result(lua_State* state) : mState(state) {
            std::size_t s = lua_gettop(mState);
            mData.reserve(s);

            for (auto i = 0; i < s; ++i) {
                mData.push_back(clg::get_from_lua<clg::ref>(state, i + 1));
            }
            lua_pop(mState, s);
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
        static dynamic_result from_lua(lua_State* l, int n) {
            return {l};
        }
    };

    template<>
    dynamic_result get_from_lua(lua_State* l) {
        return converter<dynamic_result>::from_lua(l, -1);
    }
}