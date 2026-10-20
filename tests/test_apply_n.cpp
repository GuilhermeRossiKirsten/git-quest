// Critério: gq::apply_n(f, n) deduz o tipo do valor a partir do argumento e
// do retorno de f, sem converter tudo para int.
#include <string>
#include <type_traits>

#include "check.hpp"
#include "gq/functional.hpp"

int main() {
    auto inc = [](int x) { return x + 1; };
    CHECK_EQ(gq::apply_n(inc, 5)(0), 5, "apply_n(inc, 5)(0)");
    CHECK_EQ(gq::apply_n(inc, 0)(41), 41, "apply_n(f, 0) deveria ser a identidade");

    auto halve = [](double d) { return d / 2; };
    auto halve3 = gq::apply_n(halve, 3);
    CHECK_EQ(halve3(10.0), 1.25, "apply_n(halve, 3)(10.0)");
    CHECK((std::is_same<decltype(halve3(10.0)), double>::value),
          "apply_n(halve, 3)(10.0) deveria devolver double");
    CHECK_EQ(gq::apply_n(halve, 0)(42.5), 42.5, "apply_n(halve, 0)(42.5) deveria ser a identidade");

    auto twice = [](long long v) { return v * 2; };
    auto twice40 = gq::apply_n(twice, 40);
    CHECK_EQ(twice40(1LL), 1LL << 40, "apply_n(twice, 40)(1LL)");
    CHECK((std::is_same<decltype(twice40(1LL)), long long>::value),
          "apply_n(twice, 40)(1LL) deveria devolver long long");

    return check::result();
}
