//
// Created by Alex2772 on 11/8/2021.
//

#pragma once

#include <map>
#include <memory>

namespace clg {
    struct shared_ptr_helper {
        std::shared_ptr<void> ptr;
        const type_info& type;


        template<typename T>
        shared_ptr_helper(std::shared_ptr<T> ptr):
            ptr(std::move(ptr)),
            type(typeid(T))
        {
        }

        template<typename T>
        std::shared_ptr<T> as() const {
            if (auto& expected = typeid(T); expected != type) {
                throw std::runtime_error(std::string("type mismatch: expected ") + expected.name() + ", actual " + type.name());
            }
            return reinterpret_cast<const std::shared_ptr<T>&>(ptr);
        }

    };
}