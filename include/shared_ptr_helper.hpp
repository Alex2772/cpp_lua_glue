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

    namespace impl {
        struct ptr_helper {
            virtual ~ptr_helper() = default;
        };
    }

    struct shared_ptr_helper: impl::ptr_helper {
        std::shared_ptr<void> ptr;
        const std::type_info& type;
        std::weak_ptr<void> weakPtr;
        std::function<void()> onDestroy;


        template<typename T>
        shared_ptr_helper(std::shared_ptr<T> ptr):
            ptr(convert_to_void_p(std::move(ptr))),
            type(typeid(T)),
            weakPtr(ptr)
        {
        }
        ~shared_ptr_helper() {
            if (onDestroy) onDestroy();
        }

        template<typename T>
        clg::converter_result<std::shared_ptr<T>> as() {
            if (ptr == nullptr) {
                ptr = weakPtr.lock();
            }
            if (ptr == nullptr) {
                return clg::converter_error{":destroy()-ed cpp object"};
            }
            if constexpr (std::is_base_of_v<allow_lua_inheritance, T>) {
                auto inheritance = reinterpret_cast<const std::shared_ptr<allow_lua_inheritance>&>(ptr);
                return std::dynamic_pointer_cast<T>(inheritance);
            } else {
                if (auto& expected = typeid(T); expected != type) {
                    static std::string e = std::string("type mismatch: expected ") + expected.name() + "\nnote: extend clg::allow_lua_inheritance to allow inheritance";
                    return converter_error{e.c_str()};
                }
                return reinterpret_cast<const std::shared_ptr<T>&>(ptr);
            }
        }

    private:

        template<typename T>
        std::shared_ptr<void> convert_to_void_p(std::shared_ptr<T> ptr) {
            if constexpr (std::is_base_of_v<allow_lua_inheritance, T>) {
                return std::shared_ptr<allow_lua_inheritance>(std::move(ptr));
            } else {
                return ptr;
            }
        }
    };


    struct weak_ptr_helper: impl::ptr_helper {
        std::weak_ptr<void> ptr;
        const std::type_info& type;


        template<typename T>
        weak_ptr_helper(std::weak_ptr<T> ptr):
            ptr(convert_to_void_p(std::move(ptr))),
            type(typeid(T))
        {
        }
        ~weak_ptr_helper() = default;

        template<typename T>
        std::weak_ptr<T> as() const {
            if constexpr (std::is_base_of_v<allow_lua_inheritance, T>) {
                auto inheritance = reinterpret_cast<const std::weak_ptr<allow_lua_inheritance>&>(ptr);
                return std::dynamic_pointer_cast<T>(inheritance);
            } else {
                if (auto& expected = typeid(T); expected != type) {
                    throw std::runtime_error(
                            std::string("type mismatch: expected ") + expected.name() + ", actual " + type.name() + "\nnote: extend clg::allow_lua_inheritance to allow inheritance");
                }
                return reinterpret_cast<const std::weak_ptr<T>&>(ptr);
            }
        }

    private:

        template<typename T>
        std::weak_ptr<void> convert_to_void_p(std::weak_ptr<T> ptr) {
            if constexpr (std::is_base_of_v<allow_lua_inheritance, T>) {
                return std::weak_ptr<allow_lua_inheritance>(std::move(ptr));
            } else {
                return ptr;
            }
        }
    };
}
