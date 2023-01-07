//
// Created by Alex2772 on 11/3/2021.
//

#pragma once

#include "converter.hpp"
#include "ref.hpp"
#include <map>
#include <string>

namespace clg {
    using table_array = std::vector<ref>;

    class table: public std::map<std::string, clg::ref> {
    public:
        using std::map<std::string, clg::ref>::map;

        [[nodiscard]]
        table_array toArray() const {
            table_array result;
            result.reserve(size());
            for (const auto& v : *this) {
                try {
                    size_t index = std::stoul(v.first) - 1;
                    if (result.size() <= index) {
                        result.resize(index + 1);
                    }
                    result[index] = v.second;
                } catch (...) {
                    throw clg_exception("could not convert map to array: " + v.first + " is not an integer");
                }
            }
            return result;
        }
    };

    template<>
    struct converter<clg::table> {
        static clg::table from_lua(lua_State* l, int n) {
            if (!lua_istable(l, n)) {
                clg::detail::throw_converter_error(l, n, "not a table");
            }

            clg::table result;
            if (n < 0) {
                n = lua_gettop(l) + n + 1;
            }
            lua_pushnil(l);
            while (lua_next(l, n) != 0)
            {
                // copy key so that lua_tostring should not break lua_next by modifying key
                lua_pushvalue(l, -2);
                result[clg::get_from_lua<std::string>(l, -1)] = clg::get_from_lua<clg::ref>(l, -2);
                lua_pop(l, 2);
            }
            return result;
        }

        static int to_lua(lua_State* l, const clg::table& t) {
            lua_newtable(l);

            clg::stack_integrity_check check(l);

            for (const auto&[k, v] : t) {
                clg::push_to_lua(l, k);
                clg::push_to_lua(l, v);
                lua_settable(l, -3);
            }

            return 1;
        }
    };

    template<>
    struct converter<table_array> {
        static clg::table_array from_lua(lua_State* l, int n) {
            clg::table_array result;
            auto t = converter<table>::from_lua(l, n);
            result.reserve(t.size());
            for (auto&[_, value] : t) {
                result.push_back(std::move(value));
            }
            return result;
        }

        static int to_lua(lua_State* l, const table_array& v) {
            lua_createtable(l, v.size(), 0);

            for (unsigned i = 0; i < v.size(); ++i) {
                v[i].push_value_to_stack();
                lua_rawseti(l, -2, i + 1);
            }
            return 1;
        }
    };

}