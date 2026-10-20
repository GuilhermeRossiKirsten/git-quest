// Critério: gq::compose aplica as funções na ordem matemática, aceita qualquer
// número de funções e tipos, aceita callables move-only e encaminha o
// argumento sem cópias extras.
#include <memory>
#include <string>
#include <type_traits>

#include "check.hpp"
#include "gq/functional.hpp"
#include "tracked.hpp"

int main() {
    auto inc = [](int x) { return x + 1; };
    auto dbl = [](int x) { return x * 2; };
    auto sq = [](int x) { return x * x; };

    // Ordem: compose(f, g)(x) == f(g(x))
    CHECK_EQ(gq::compose(inc, dbl)(5), 11, "compose(inc, dbl)(5) deveria ser inc(dbl(5))");
    CHECK_EQ(gq::compose(dbl, inc)(5), 12, "compose(dbl, inc)(5) deveria ser dbl(inc(5))");
    CHECK_EQ(gq::compose(inc)(1), 2, "compose(f)(x) deveria ser f(x)");
    CHECK_EQ(gq::compose(inc, dbl, sq)(3), 19, "compose(inc, dbl, sq)(3) deveria ser inc(dbl(sq(3)))");
    CHECK_EQ(gq::compose(sq, inc, dbl, inc)(1), 25, "compose com 4 funções deveria ser sq(inc(dbl(inc(1))))");

    // Outros tipos: std::string
    auto add_a = [](std::string s) { return s + "a"; };
    auto add_b = [](std::string s) { return s + "b"; };
    CHECK_EQ(gq::compose(add_a, add_b)(std::string("x")), std::string("xba"),
             "compose(add_a, add_b)(\"x\") deveria ser add_a(add_b(\"x\"))");

    // Funções que mudam o tipo: int -> int -> double
    auto plus_half = [](double d) { return d + 0.5; };
    auto halve_int = [](int i) { return i / 2; };
    auto mixed = gq::compose(plus_half, halve_int);
    CHECK_EQ(mixed(5), 2.5, "compose(plus_half, halve_int)(5) deveria ser plus_half(halve_int(5))");
    CHECK((std::is_same<decltype(mixed(5)), double>::value),
          "compose(plus_half, halve_int)(5) deveria ter o tipo de retorno de plus_half (double)");

    // Callable move-only (dono de um unique_ptr)
    auto offset = [p = std::make_unique<int>(10)](int x) { return x + *p; };
    auto owned = gq::compose(std::move(offset), sq);
    CHECK_EQ(owned(3), 19, "compose(offset_move_only, sq)(3) deveria ser offset(sq(3))");

    // Encaminhamento do argumento: nenhuma cópia além das que as funções exigem.
    auto id = [](Tracked t) { return t; };
    Tracked::reset();
    Tracked r = gq::compose(id, id)(Tracked(7, "payload grande o bastante para não caber no SSO"));
    CHECK_EQ(r.key, 7, "compose(id, id)(t).key");
    CHECK_EQ(Tracked::copies, 0, "cópias ao passar um temporário por compose(id, id)");

    Tracked lvalue(8, "outro payload grande o bastante para não caber no SSO");
    Tracked::reset();
    Tracked r2 = gq::compose(id, id)(lvalue);
    CHECK_EQ(r2.key, 8, "compose(id, id)(lvalue).key");
    CHECK_EQ(Tracked::copies, 1, "cópias ao passar um lvalue por compose(id, id) (só a exigida por id)");

    return check::result();
}
