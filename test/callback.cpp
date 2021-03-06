
#include <boost/test/unit_test.hpp>
#include <clg/clg.hpp>

BOOST_AUTO_TEST_SUITE(callback)



    BOOST_AUTO_TEST_CASE(check1) {
        clg::vm v;

        clg::function callback;

        v.register_function("register_callback", [&](int two, clg::function function) {
            BOOST_CHECK_EQUAL(two, 2);
            callback = std::move(function);
        });
        v.register_function("call_callback", [&] {
            return callback.call<int>(8, 3);
        });

        auto result = v.do_string<int>(R"(
register_callback(2, function(a, b)
  return a - b
end)
return call_callback() - 1
)");
        BOOST_CHECK_EQUAL(result, 4);
    }

BOOST_AUTO_TEST_SUITE_END()