//
// Created by alex2 on 02.05.2021.
//

#pragma once

#define CLG_TRACE_CALLS 0

#include "lua.hpp"
#include "converter.hpp"
#include "dynamic_result.hpp"
#include "function.hpp"
#include "util.hpp"
#include "vararg.hpp"
#include "shared_ptr_helper.hpp"
#include "magic_enum.hpp"

#include <cstring>
#include <thread>
#include <sstream>
#include "cfunction.hpp"

namespace clg {
    template<class C>
    class class_registrar;

    namespace impl {
        struct Method {
            std::string name;
            lua_CFunction cFunction;
        };
    }
    using lua_cfunctions = std::vector<impl::Method>;


    struct class_inherit_metainfo {
        std::function<bool(clg::allow_lua_inheritance* child)> isBaseOf;
        lua_cfunctions methods;
    };


    /**
     * Базовый интерфейс для работы с Lua. Не инициализирует Lua самостоятельно.
     */
    class state_interface {
    private:
        lua_State* mState;
        std::thread::id mMyThread = std::this_thread::get_id();

        void throw_syntax_error() {
            auto s = pop_from_lua<std::string>(mState);
            throw lua_exception(std::move(s));
        }

        void register_function_raw(const std::string& name, lua_CFunction function) {
            lua_register(mState, name.c_str(), function);
        }


        std::vector<class_inherit_metainfo> mClassMetainfo;

    public:


        std::vector<class_inherit_metainfo>& class_metainfo() {
            return mClassMetainfo;
        }


        template<class...>
        struct types {
            using type = types;
        };

        template<typename Sig>
        struct callable_class_info;

        template<typename Class, typename R, typename... Args>
        struct callable_class_info<R(Class::*)(Args...) const> {
            using class_t = Class;
            using args = types<Args...>;
            using return_t = R;
            static constexpr bool is_noexcept = false;
            static constexpr bool is_const = true;
        };
        template<typename Class, typename R, typename... Args>
        struct callable_class_info<R(Class::*)(Args...)> {
            using class_t = Class;
            using args = types<Args...>;
            using return_t = R;
            static constexpr bool is_noexcept = false;
            static constexpr bool is_const = false;
        };

        template<typename Class, typename R, typename... Args>
        struct callable_class_info<R(Class::*)(Args...) const noexcept> {
            using class_t = Class;
            using args = types<Args...>;
            using return_t = R;
            static constexpr bool is_noexcept = true;
            static constexpr bool is_const = true;
        };
        template<typename Class, typename R, typename... Args>
        struct callable_class_info<R(Class::*)(Args...) noexcept> {
            using class_t = Class;
            using args = types<Args...>;
            using return_t = R;
            static constexpr bool is_noexcept = true;
            static constexpr bool is_const = false;
        };

        template<typename R, typename... Args>
        struct callable_class_info<R(*)(Args...)> {
            using args = types<Args...>;
            using return_t = R;
        };

        template<typename Callable>
        struct callable_helper {
            using function_info = callable_class_info<decltype(&Callable::operator())>;

            template<typename... Args>
            struct wrapper_function_helper_t {};
            template<typename... Args>
            struct wrapper_function_helper_t<types<Args...>> {
                static typename function_info::return_t wrapper_function(Args... args) {
                    if (std::is_same_v<typename function_info::return_t, void>) {
                        (*callable())(std::move(args)...);
                    } else {
                        return (*callable())(std::move(args)...);
                    }
                }
            };

            using wrapper_function_helper = wrapper_function_helper_t<typename function_info::args>;

            static Callable*& callable() {
                static Callable* callable = nullptr;
                return callable;
            }

        };


        template<typename... Callables>
        struct overloaded_helper {
            static int fake_lua_cfunction(lua_State* L) noexcept {
                std::string errorDescription;
                auto stack = lua_gettop(L);
                for (const auto& func : callable()) {
                    try {
                        auto r = func(L);
                        if (r != OVERLOADED_HELPER_SUBSTITUTION_FAILURE) {
                            return r;
                        }
                    } catch (const clg::substitution_error& e) {
                        if (!errorDescription.empty()) {
                            errorDescription += "; ";
                        }
                        errorDescription += e.what();
                    }
                    assert(lua_gettop(L) == stack);
                }
                luaL_error(L, "overloaded function substitution error: %s", errorDescription.c_str());
                return 0;
            }

            static std::vector<lua_CFunction>& callable() {
                static std::vector<lua_CFunction> callable;
                return callable;
            }
        };

        state_interface(lua_State* state) : mState(state) {

        }

        void init_global_functions();


        template<class C>
        class_registrar<C> register_class() {
            return class_registrar<C>(*this);
        }

        template<auto f>
        void register_function(const std::string& name) {
            register_function_raw(name, cfunction<f>(name));
        }

        template<typename Callable>
        void register_function(const std::string& name, Callable callable) {
            using helper = callable_helper<Callable>;
            helper::callable() = new Callable(std::move(callable));
            register_function<helper::wrapper_function_helper::wrapper_function>(name);
        }

        template<typename FirstCallable, typename... RestCallables>
        std::vector<lua_CFunction>& register_function_overloaded(const std::string& name, FirstCallable&& firstCallable, RestCallables&&... restCallables) {
            using helper = overloaded_helper<FirstCallable, RestCallables...>;
            auto& callable = helper::callable();
            callable = { wrap_lambda_to_cfunction_for_overloading(std::forward<FirstCallable>(firstCallable), name),
                         wrap_lambda_to_cfunction_for_overloading(std::forward<RestCallables>(restCallables), name)...  };
            register_function_raw(name, helper::fake_lua_cfunction);
            return helper::callable();
        }


        template<typename Callable, bool passthroughSubstitutionError = false>
        lua_CFunction wrap_lambda_to_cfunction(Callable&& callable, const std::string& name) {
            using helper = callable_helper<Callable>;
            constexpr auto f = helper::wrapper_function_helper::wrapper_function;
            using my_register_function_helper = decltype(clg::detail::make_register_function_helper(f));
            using my_instance = typename my_register_function_helper::template instance<f, passthroughSubstitutionError>;

#if CLG_TRACE_CALLS
            my_instance::trace_name() = name;
#endif

            delete helper::callable();
            helper::callable() = new Callable(std::forward<Callable>(callable));

            return my_instance::call;
        }

        template<typename Callable>
        lua_CFunction wrap_lambda_to_cfunction_for_overloading(Callable&& callable, const std::string& traceName) { // used only for register_function_overloaded
            return wrap_lambda_to_cfunction<Callable, true>(std::forward<Callable>(callable), traceName);
        }

        template<typename ReturnType = void>
        ReturnType do_string(const std::string& exec) {
            if (luaL_dostring(mState, exec.c_str()) != 0) {
                throw_syntax_error();
            }
            if constexpr (!std::is_same_v<void, ReturnType>) {
                return get_from_lua<ReturnType>(mState);
            }
        }
        template<typename ReturnType = void>
        ReturnType do_file(const std::string& exec) {
            if (luaL_dofile(mState, exec.c_str()) != 0) {
                throw_syntax_error();
            }
            if constexpr (!std::is_same_v<void, ReturnType>) {
                return get_from_lua<ReturnType>(mState);
            }
        }

        operator lua_State*() const {
            assert(("multithreading is not supported", mMyThread == std::this_thread::get_id()));
            return mState;
        }

        template<typename Enum>
        static int to_int_mapper(Enum value) {
            return static_cast<int>(value);
        }
        template<typename T, typename Mapper = decltype(to_int_mapper<T>)>
        void register_enum(const char* name = nullptr, Mapper&& mapper = to_int_mapper<T>) noexcept {
            static_assert(std::is_enum_v<T>, "T expected to be enum");
            stack_integrity_check checks(mState);

            lua_createtable(mState, 0, magic_enum::enum_values<T>().size());

            for (const auto value : magic_enum::enum_values<T>()) {
                lua_pushstring(mState, magic_enum::enum_name(value).data());
                clg::push_to_lua(mState, mapper(value));
                lua_settable(mState, -3);
            }
            lua_setglobal(mState, name ? name : clg::class_name<T>().c_str());
        };


        template<typename T>
        void set_global_value(std::string_view name, const T& value) noexcept {
            clg::stack_integrity_check check(*this);
            push_to_lua(mState, value);
            lua_setglobal(mState, name.data());
        }



        /**
         * @brief Получение глобальной функции.
         * @param v название функции для вызова
         * @return обёртка для передачи аргументов в функцию
         */
        function global_function(std::string_view v) {
            lua_getglobal(mState, v.data());
            return {clg::ref::from_stack(*this)};
        }
        /**
         * @brief Получение глобальной переменной.
         * @param v название переменной для вызова
         * @return ref
         */
        ref global_variable(std::string_view v) {
            lua_getglobal(mState, v.data());
            return clg::ref::from_stack(*this);
        }


        void collectGarbage() {
            lua_gc(mState, LUA_GCCOLLECT, 0);
        }
    };

    /**
     * В отличии от interface, этот класс сам создаёт виртуальную машину Lua, загружает базовые библиотеки и отвечает за
     * её освобождение.
     */
    class vm: public state_interface, impl::raii_state_updater {
    public:
        vm(): state_interface(luaL_newstate()), impl::raii_state_updater(state_interface::operator lua_State *()) {
            luaL_openlibs(*this);
            init_global_functions();
        }
        ~vm() {
            function::error_callback() = {};
            lua_close(*this);
        }

    };


    namespace impl {
        template<typename... Args>
        struct select_overload {
            template<typename R, typename C>
            constexpr auto operator()(R(C::*ptr)(Args...)) const noexcept -> decltype(ptr) {
                return ptr;
            }
        };
    }
    /**
     * @brief Chooses specific overload of a method.
     * @ingroup useful_traits
     * @details
     * Example:
     * @code{cpp}
     * struct GameObject {
     * public:
     *   void setPos(glm::vec3);
     *   void setPos(glm::vec2);
     * };
     * ...
     * auto setPosVec2 = clg::select_overload<glm::vec2>::of(&GameObject::setPos);
     * @endcode
     */
    template<typename... Args>
    constexpr impl::select_overload<Args...> select_overload = {};
}

#include "class_registrar.hpp"
#include "table.hpp"
#include "object_expose.hpp"

inline std::string clg::any_to_string(lua_State* l, int n, int depth, bool showMetatable) {
    if (depth <= 0) {
        return "<depth exceeded>";
    }
    std::stringstream ss;

    bool metatableDetected = false;
    if (showMetatable && lua_getmetatable(l, n)) {
        metatableDetected = true;
        ss << "[ ";
        lua_pop(l, 1);
    }
    if (auto str = lua_touserdata(l, n)) {
        ss << "\"<userdata>\"";
    } else if (lua_isnil(l, n)) {
        ss << "\"<nil>\"";
    } else if (lua_isfunction(l, n)) {
        ss << "\"<function>\"";
    } else if (lua_istable(l, n)) {
        ss << "{ ";
        bool first = true;
        for (const auto&[k, v] : clg::get_from_lua<clg::table>(l, n)) {
            if (first) {
                first = false;
            } else {
                ss << ", ";
            }
            v.push_value_to_stack();
            ss << "\"" << k << "\": " << any_to_string(l, -1, depth - 1, showMetatable);
            lua_pop(l, 1);
        }
        ss << " }";
    } else if (lua_isnumber(l, n)) {
        ss << lua_tonumber(l, n);
    } else if (auto str = lua_tostring(l, n)) {
        ss << '"' << str << '"';
    } else if (lua_isstring(l, n)) {
        ss << "\"" << lua_tostring(l, n) << "\"";
    } else {
        ss << "\"?\"";
    }

    if (showMetatable && lua_getmetatable(l, n)) {
        assert(metatableDetected);
        ss << ", " << any_to_string(l, -1, depth - 1) << " ]";
        lua_pop(l, 1);
    }


    return ss.str();
}

#include "any_wrap.hpp"
template<typename... Args>
void clg::ref::invokeNullsafe(Args&& ... args) {
    if (isFunction()) {
        clg::function(*this)(std::forward<Args>(args)...);
    }
}

inline void clg::state_interface::init_global_functions() {
    register_class<any_wrap>();
}