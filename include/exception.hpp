//
// Created by alex2 on 02.05.2021.
//

#pragma once

#include <functional>
#include <stdexcept>

namespace clg {

    class clg_exception : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
        ~clg_exception() override = default;
        static inline std::function<void(std::exception_ptr)> errorCallback;
    };
    class lua_exception: public clg_exception {
    public:
        using clg_exception::clg_exception;
        ~lua_exception() override = default;
    };


    struct substitution_error: clg_exception {
        using clg_exception::clg_exception;
    };
}