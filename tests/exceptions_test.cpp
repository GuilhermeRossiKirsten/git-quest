// Grupo "exceptions": entrada malformada é rotina num log de produção, não um
// evento excepcional. Nenhuma exceção pode ser criada — nem capturada
// internamente — ao analisar linhas inválidas. O contador é alimentado pelo
// executor (main_test.cpp), que observa toda criação de exceção do processo.
#include "check.hpp"
#include "support.hpp"

namespace {

constexpr std::string_view kMalformed[] = {
    "",
    "   \r",
    "2026-10-19T12:00:01Z GET /x 200",
    "2026-10-19T12:00:01Z GET /x 200 1ms extra",
    "2026-13-45T99:00:00Z GET /x 200 1ms",
    "2026-10-19T12:00:01Z FETCH /x 200 1ms",
    "2026-10-19T12:00:01Z GET x 200 1ms",
    "2026-10-19T12:00:01Z GET /x abc 1ms",
    "2026-10-19T12:00:01Z GET /x 2OO 1ms",
    "2026-10-19T12:00:01Z GET /x 99999999999 1ms",
    "2026-10-19T12:00:01Z GET /x 999 1ms",
    "2026-10-19T12:00:01Z GET /x 200 abcms",
    "2026-10-19T12:00:01Z GET /x 200 -5ms",
    "2026-10-19T12:00:01Z GET /x 200 99999999999999999999ms",
    "2026-10-19T12:00:01Z GET /x 200 12",
};

}  // namespace

GQ_TEST("exceptions", "parse_line não cria exceções para linhas inválidas") {
    for (auto line : kMalformed) {
        const auto before = gq::g_exceptions_allocated;
        auto r = logan::parse_line(line);
        GQ_CHECK(!r.has_value());
        const auto created = gq::g_exceptions_allocated - before;
        if (created != 0) {
            ::gq::report_failure(__FILE__, __LINE__,
                                 std::format("parse_line({}) criou {} exceção(ões)", gq::show(line), created));
        }
    }
}

GQ_TEST("exceptions", "analyze não cria exceções para um log cheio de erros") {
    std::string text;
    for (auto line : kMalformed) {
        text += line;
        text += '\n';
    }
    const auto before = gq::g_exceptions_allocated;
    auto rep = logan::analyze(text);
    const auto created = gq::g_exceptions_allocated - before;
    GQ_CHECK_EQ(created, std::size_t{0});
    GQ_CHECK_EQ(rep.valid, std::size_t{0});
}
