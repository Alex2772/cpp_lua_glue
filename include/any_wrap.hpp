#pragma once

#include "object_expose.hpp"
#include <any>

namespace clg {
    class any_wrap: public lua_self, public std::any {
    public:
        using std::any::any;
    };
}