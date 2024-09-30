#pragma once

namespace clg {
    class table_view: public ref {
    public:
        using ref::ref;

        template<typename Key>
        struct value_view {
        public:
            value_view(const table_view& table, Key&& name) : table(table), key(std::forward<Key>(name)) {}

            template<typename T>
            [[nodiscard]]
            T as() const {
                const auto l = clg::state();
                stack_integrity_check check(l);
                table.push_value_to_stack(l);
                if (!lua_istable(l, -1)) {
                    lua_pop(l, 1);
                    throw clg_exception("not a table view");
                }
                clg::push_to_lua(l, key);
                lua_gettable(l, -2);
                auto v = clg::get_from_lua<T>(l);
                lua_pop(l, 2);
                return v;
            }

            template<typename T>
            [[nodiscard]]
            std::optional<T> is() const {
                const auto l = clg::state();
                stack_integrity_check check(l);
                table.push_value_to_stack(l);
                if (!lua_istable(l, -1)) {
                    lua_pop(l, 1);
                    throw clg_exception("not a table view");
                }
                clg::push_to_lua(l, key);
                lua_gettable(l, -2);
                if (lua_isnil(l, -1)) {
                    lua_pop(l, 2);
                    return std::nullopt;
                }
                auto v = clg::get_from_lua_raw<T>(l);
                lua_pop(l, 2);
                if (v.is_error()) {
                    return std::nullopt;
                }
                return *v;
            }


            [[nodiscard]]
            clg::ref ref() const {
                clg::stack_integrity_check c;
                return as<clg::ref>();
            }

            [[nodiscard]]
            explicit operator class ref() const {
                return ref();
            }

            template<typename T>
            const T& operator=(const T& t) const noexcept {
                const auto l = clg::state();
                clg::stack_integrity_check c(l);
                table.push_value_to_stack(l);
                clg::push_to_lua(l, key);
                clg::push_to_lua(l, t);
                lua_settable(l, -3);
                lua_pop(l, 1);
                return t;
            }

            template<typename... Args>
            void invokeNullsafe(Args&&... args) {
                ref().invokeNullsafe(std::forward<Args>(args)...);
            }
        private:
            const table_view& table;
            Key key;
        };

        table_view(ref r): ref(std::move(r)) {
        }


        template<typename K, typename V>
        void raw_set(const K& key, const V& value, lua_State* L = clg::state()) const noexcept {
            clg::stack_integrity_check c(L);
            push_value_to_stack(L);
            clg::push_to_lua(L, key);
            clg::push_to_lua(L, value);
            lua_rawset(L, -3);
            lua_pop(L, 1);
        }

        size_t size(lua_State* L = clg::state()) const noexcept {
            clg::stack_integrity_check c;
            push_value_to_stack(L);
            lua_len(L, -1);
            auto v = clg::get_from_lua<size_t>(L);
            lua_pop(L, 2);
            return v;
        }

        template<typename Key>
        value_view<Key> operator[](Key&& k) const {
            assert(!isNull());
            return { *this, std::forward<Key>(k) };
        }

        template<typename Key, typename Factory>
        value_view<Key> get_or_create(Key&& key, Factory&& valueFactory) {
            assert(!isNull());
            value_view<Key> result = (*this)[std::forward<Key>(key)];
            if (result.ref().isNull()) {
                result = valueFactory();
            }
            return result;
        }
    };

    template<>
    struct converter<clg::table_view> {
        static converter_result<table_view> from_lua(lua_State* l, int n) {
            lua_pushvalue(l, n);
            if (!lua_istable(l, -1)) {
                return converter_error{"not a table"};
            }
            return table_view(clg::ref::from_stack(l));
        }
        static int to_lua(lua_State* l, const clg::ref& ref) {
            return clg::push_to_lua(l, ref);
        }
    };
}
