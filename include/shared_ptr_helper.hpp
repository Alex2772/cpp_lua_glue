//
// Created by Alex2772 on 11/8/2021.
//

#pragma once

#include <map>
#include <memory>

namespace clg {
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


        template<typename T>
        shared_ptr_helper(std::shared_ptr<T> ptr):
            ptr(convert_to_void_p(std::move(ptr))),
            type(typeid(T))
        {
        }
        ~shared_ptr_helper() = default;

        template<typename T>
        std::shared_ptr<T> as() const {
            if constexpr (std::is_base_of_v<allow_lua_inheritance, T>) {
                auto inheritance = reinterpret_cast<const std::shared_ptr<allow_lua_inheritance>&>(ptr);
                return std::dynamic_pointer_cast<T>(inheritance);
            } else {
                if (auto& expected = typeid(T); expected != type) {
                    throw std::runtime_error(
                            std::string("type mismatch: expected ") + expected.name() + ", actual " + type.name() + "\nnote: extend clg::allow_lua_inheritance to allow inheritance");
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