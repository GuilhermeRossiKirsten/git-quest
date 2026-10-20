// Critério: closures devolvidas por gq::tagger e gq::at_least continuam
// válidas depois que a função que as criou retornou.
// Rode com AddressSanitizer (o avaliador já faz isso).
#include <string>

#include "check.hpp"
#include "gq/functional.hpp"

int main() {
    auto tag = gq::tagger("auditoria-financeira-2024");
    CHECK_EQ(tag("estorno"), std::string("[auditoria-financeira-2024] estorno"), "tagger");
    CHECK_EQ(tag("bloqueio"), std::string("[auditoria-financeira-2024] bloqueio"), "tagger, segunda chamada");

    auto after_m = gq::at_least(std::string("m-limite-longo-o-suficiente-para-o-heap"));
    CHECK(after_m("zeta"), "at_least(string): \"zeta\" >= limite");
    CHECK(!after_m("alfa"), "at_least(string): \"alfa\" < limite");

    auto ten = gq::at_least(10);
    CHECK(ten(10), "at_least(10)(10)");
    CHECK(ten(11), "at_least(10)(11)");
    CHECK(!ten(9), "at_least(10)(9) deveria ser false");

    auto half = gq::at_least(0.5);
    CHECK(half(0.75), "at_least(0.5)(0.75)");
    CHECK(!half(0.25), "at_least(0.5)(0.25) deveria ser false");

    return check::result();
}
