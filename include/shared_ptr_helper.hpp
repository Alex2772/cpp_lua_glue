//
// Created by Alex2772 on 11/8/2021.
//

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <variant>

namespace clg {
    struct converter_error {
        const char* errorLiteral;
    };


    /**
     * @brief Wraps clg::converter<>::from_lua return value, as such it can report conversion errors without exceptions.
     */
    template<typename T>
    struct converter_result {
        using impl = std::variant<T, converter_error>;

        template<typename U, typename = std::enable_if_t<std::is_convertible_v<U, T>>>
        converter_result(U&& value): value(T((std::forward<U>(value)))) {}
        converter_result(converter_error value): value(std::move(value)) {}

        [[nodiscard]]
        bool is_ok() const noexcept {
            return std::holds_alternative<T>(value);
        }

        [[nodiscard]]
        bool is_error() const noexcept {
            return std::holds_alternative<converter_error>(value);
        }

        [[nodiscard]]
        T& operator*() noexcept {
            assert(is_ok());
            return *std::get_if<T>(&value);
        }

        [[nodiscard]]
        const T& operator*() const noexcept {
            assert(is_ok());
            return *std::get_if<T>(&value);
        }

        [[nodiscard]]
        converter_error& error() noexcept {
            assert(is_error());
            return *std::get_if<converter_error>(&value);
        }

        [[nodiscard]]
        const converter_error& error() const noexcept {
            assert(is_error());
            return *std::get_if<converter_error>(&value);
        }

    private:
        impl value;
    };

    class allow_lua_inheritance {
    public:
        virtual ~allow_lua_inheritance() = default;
    };

    class userdata_helper {
        using shared_ptr = std::shared_ptr<void>;
        using weak_ptr = std::weak_ptr<void>;
    public:
        template<typename T>
        userdata_helper(std::shared_ptr<T> ptr) : mPtr(weak_ptr(convert_to_void_p(std::move(ptr)))), mType(typeid(T)) {
        }

        template<typename T>
        clg::converter_result<std::shared_ptr<T>> as() {
            auto ptr = std::visit([](const auto& ptr) -> shared_ptr {
                using type = std::decay_t<decltype(ptr)>;
                if constexpr (std::is_same_v<type, shared_ptr>) {
                    return ptr;
                }
                else {
                    return ptr.lock();
                }
            }, mPtr);
            if constexpr (std::is_base_of_v<allow_lua_inheritance, T>) {
                auto inheritance = reinterpret_cast<const std::shared_ptr<allow_lua_inheritance>&>(ptr);
                return std::dynamic_pointer_cast<T>(inheritance);
            }
            else {
                if (auto& expected = typeid(T); expected != mType) {
                    static std::string e = std::string("type mismatch: expected ") + expected.name() + "\nnote: extend clg::allow_lua_inheritance to allow inheritance";
                    return converter_error{e.c_str()};
                }
                return reinterpret_cast<const std::shared_ptr<T>&>(ptr);
            }
        }

        void switch_to_shared() {
            if (auto weak = std::get_if<weak_ptr>(&mPtr)) {
                mPtr = weak->lock();
            }
        }

        void switch_to_weak() {
            if (auto shared = std::get_if<shared_ptr>(&mPtr)) {
                mPtr = weak_ptr(*shared);
            }
        }

    private:
        std::variant<shared_ptr, weak_ptr> mPtr;
        const std::type_info& mType;

        template<typename T>
        std::shared_ptr<void> convert_to_void_p(std::shared_ptr<T> ptr) {
            if constexpr (std::is_base_of_v<allow_lua_inheritance, T>) {
                return std::shared_ptr<allow_lua_inheritance>(std::move(ptr));
            } else {
                return ptr;
            }
        }
    };
}
