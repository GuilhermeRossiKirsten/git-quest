// Critério: cada gq::make_counter() tem o seu próprio estado; cópias de um
// contador também seguem independentes.
#include "check.hpp"
#include "gq/functional.hpp"

int main() {
    auto a = gq::make_counter();
    CHECK_EQ(a(), 0, "a(): primeiro valor de make_counter()");
    CHECK_EQ(a(), 1, "a(): segundo valor");

    auto b = gq::make_counter(100);
    CHECK_EQ(b(), 100, "b(): primeiro valor de make_counter(100)");
    CHECK_EQ(a(), 2, "a() depois de criar e usar b (contadores independentes)");
    CHECK_EQ(b(), 101, "b() depois de usar a");

    auto c = a;  // cópia: continua de onde a parou, mas não compartilha estado
    CHECK_EQ(c(), 3, "c() (cópia de a)");
    CHECK_EQ(c(), 4, "c() de novo");
    CHECK_EQ(a(), 3, "a() não deveria ser afetado pela cópia c");

    auto d = gq::make_counter(-2);
    CHECK_EQ(d(), -2, "d(): make_counter(-2)");
    CHECK_EQ(d(), -1, "d(): segundo valor");
    CHECK_EQ(b(), 102, "b() não deveria ser afetado por d");

    return check::result();
}
