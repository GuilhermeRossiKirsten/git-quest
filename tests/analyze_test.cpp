// Grupo "analyze": estatísticas, contagem de linhas e erros com número de linha.
#include "check.hpp"
#include "support.hpp"

#include <string>

using logan::analyze;

GQ_TEST("analyze", "log de referência: contagens") {
    auto rep = analyze(gq::kSampleLog);
    GQ_CHECK_EQ(rep.lines, 8u);
    GQ_CHECK_EQ(rep.valid, 5u);
    GQ_CHECK_EQ(rep.blank, 1u);
    GQ_CHECK_EQ(rep.errors.size(), 2u);
    GQ_CHECK_EQ(gq::describe(rep.by_status), std::string("200=3 204=1 404=1"));
}

GQ_TEST("analyze", "log de referência: erros com linha 1-based e motivo") {
    auto rep = analyze(gq::kSampleLog);
    GQ_CHECK_EQ(gq::describe(rep.errors), std::string("linha 4: bad_status@campo 4; linha 7: bad_latency@campo 5"));
}

GQ_TEST("analyze", "log de referência: percentis e top paths") {
    auto rep = analyze(gq::kSampleLog);
    GQ_CHECK_EQ(rep.p50_ms, 30u);
    GQ_CHECK_EQ(rep.p95_ms, 250u);
    GQ_CHECK_EQ(gq::describe(rep.top_paths), std::string("/api/items=2, /api/items/7=2, /health=1"));
}

GQ_TEST("analyze", "texto vazio não tem linhas") {
    auto rep = analyze("");
    GQ_CHECK_EQ(rep.lines, 0u);
    GQ_CHECK_EQ(rep.valid, 0u);
    GQ_CHECK_EQ(rep.blank, 0u);
    GQ_CHECK_EQ(rep.p50_ms, 0u);
    GQ_CHECK_EQ(rep.p95_ms, 0u);
    GQ_CHECK(rep.errors.empty());
    GQ_CHECK(rep.top_paths.empty());
    GQ_CHECK(rep.by_status.empty());
}

GQ_TEST("analyze", "um '\\n' sozinho é uma linha em branco") {
    auto rep = analyze("\n");
    GQ_CHECK_EQ(rep.lines, 1u);
    GQ_CHECK_EQ(rep.blank, 1u);
    GQ_CHECK(rep.errors.empty());
}

GQ_TEST("analyze", "a última linha conta com ou sem '\\n' final") {
    const std::string two = gq::log_line("/a", 1) + gq::log_line("/b", 2);
    auto with_newline = analyze(two);
    auto without_newline = analyze(std::string_view(two).substr(0, two.size() - 1));
    GQ_CHECK_EQ(with_newline.lines, 2u);
    GQ_CHECK_EQ(with_newline.valid, 2u);
    GQ_CHECK_EQ(without_newline.lines, 2u);
    GQ_CHECK_EQ(without_newline.valid, 2u);
    auto trailing_blank = analyze(two + "\n");
    GQ_CHECK_EQ(trailing_blank.lines, 3u);
    GQ_CHECK_EQ(trailing_blank.blank, 1u);
}

GQ_TEST("analyze", "linhas em branco contam na numeração dos erros") {
    auto rep = analyze("\n   \n2026-10-19T12:00:01Z FETCH /x 200 1ms\n\t\n2026-10-19T12:00:01Z GET /x 200\n");
    GQ_CHECK_EQ(rep.lines, 5u);
    GQ_CHECK_EQ(rep.blank, 3u);
    GQ_CHECK_EQ(gq::describe(rep.errors), std::string("linha 3: bad_method@campo 2; linha 5: missing_fields@campo 5"));
}

GQ_TEST("analyze", "todo tipo de linha inválida aparece em errors, nenhuma é engolida") {
    std::string text;
    text += "2026-10-19T12:00:01Z GET /ok 200 1ms\n";        // 1
    text += "2026-10-19T12:00:01Z GET /x 200\n";             // 2 missing_fields
    text += "2026-10-19T12:00:01Z GET /x 200 1ms extra\n";   // 3 extra_fields
    text += "2026-02-30T12:00:01 GET /x 200 1ms\n";          // 4 bad_timestamp
    text += "2026-10-19T12:00:01Z get /x 200 1ms\n";         // 5 bad_method
    text += "2026-10-19T12:00:01Z GET x 200 1ms\n";          // 6 bad_path
    text += "2026-10-19T12:00:01Z GET /x abc 1ms\n";         // 7 bad_status
    text += "2026-10-19T12:00:01Z GET /x 999 1ms\n";         // 8 bad_status
    text += "2026-10-19T12:00:01Z GET /x 200 abc\n";         // 9 bad_latency
    text += "2026-10-19T12:00:01Z GET /x 200 -1ms\n";        // 10 bad_latency
    auto rep = analyze(text);
    GQ_CHECK_EQ(rep.lines, 10u);
    GQ_CHECK_EQ(rep.valid, 1u);
    GQ_CHECK_EQ(gq::describe(rep.errors),
                std::string("linha 2: missing_fields@campo 5; linha 3: extra_fields@campo 6; "
                            "linha 4: bad_timestamp@campo 1; linha 5: bad_method@campo 2; "
                            "linha 6: bad_path@campo 3; linha 7: bad_status@campo 4; "
                            "linha 8: bad_status@campo 4; linha 9: bad_latency@campo 5; "
                            "linha 10: bad_latency@campo 5"));
    GQ_CHECK_EQ(gq::describe(rep.by_status), std::string("200=1"));
}

GQ_TEST("analyze", "CRLF produz exatamente o mesmo resultado que LF") {
    auto lf = analyze(gq::kSampleLog);
    auto crlf = analyze(gq::to_crlf(gq::kSampleLog));
    GQ_CHECK_EQ(crlf.lines, lf.lines);
    GQ_CHECK_EQ(crlf.valid, lf.valid);
    GQ_CHECK_EQ(crlf.blank, lf.blank);
    GQ_CHECK_EQ(gq::describe(crlf.errors), gq::describe(lf.errors));
    GQ_CHECK_EQ(gq::describe(crlf.by_status), gq::describe(lf.by_status));
    GQ_CHECK_EQ(gq::describe(crlf.top_paths), gq::describe(lf.top_paths));
    GQ_CHECK_EQ(crlf.p95_ms, lf.p95_ms);
}

GQ_TEST("analyze", "lines = valid + inválidas + em branco") {
    for (std::string_view text : {gq::kSampleLog, std::string_view("\n\n\n"), std::string_view("x\ny\n\nz"),
                                  std::string_view("2026-10-19T12:00:01Z GET /x 2OO 1ms")}) {
        auto rep = analyze(text);
        GQ_CHECK_EQ(rep.lines, rep.valid + rep.errors.size() + rep.blank);
    }
}

GQ_TEST("analyze", "percentis pelo método nearest-rank") {
    std::string text;
    for (std::uint32_t v : {7u, 3u, 20u, 1u, 15u, 9u, 12u, 18u, 2u, 11u, 5u, 14u, 19u, 4u, 16u, 6u, 13u, 8u, 17u, 10u}) {
        text += gq::log_line("/p", v);  // 1..20 embaralhados
    }
    auto rep = analyze(text);
    GQ_CHECK_EQ(rep.p50_ms, 10u);
    GQ_CHECK_EQ(rep.p95_ms, 19u);

    auto two = analyze(gq::log_line("/p", 20) + gq::log_line("/p", 10));
    GQ_CHECK_EQ(two.p50_ms, 10u);
    GQ_CHECK_EQ(two.p95_ms, 20u);

    auto four = analyze(gq::log_line("/p", 40) + gq::log_line("/p", 10) + gq::log_line("/p", 30) +
                        gq::log_line("/p", 20));
    GQ_CHECK_EQ(four.p50_ms, 20u);
    GQ_CHECK_EQ(four.p95_ms, 40u);

    auto one = analyze(gq::log_line("/p", 42));
    GQ_CHECK_EQ(one.p50_ms, 42u);
    GQ_CHECK_EQ(one.p95_ms, 42u);
}

GQ_TEST("analyze", "latências inválidas não entram nos percentis") {
    auto rep = analyze(gq::log_line("/p", 10) + "2026-10-19T12:00:01Z GET /p 200 -1ms\n" + gq::log_line("/p", 20));
    GQ_CHECK_EQ(rep.valid, 2u);
    GQ_CHECK_EQ(rep.p95_ms, 20u);
}

GQ_TEST("analyze", "top paths: no máximo 3, empate desempata pelo path") {
    std::string text;
    for (auto path : {"/c", "/b", "/a", "/d", "/c", "/b", "/a", "/caf\xC3\xA9", "/caf\xC3\xA9", "/caf\xC3\xA9"}) {
        text += gq::log_line(path, 1);
    }
    auto rep = analyze(text);
    GQ_CHECK_EQ(gq::describe(rep.top_paths), std::string("/caf\xC3\xA9=3, /a=2, /b=2"));
}

GQ_TEST("analyze", "log grande com paths curtos repetidos") {
    std::string text;
    for (int i = 0; i < 1000; ++i) {
        text += gq::log_line(std::format("/p{}", i % 7), static_cast<std::uint32_t>(i % 100 + 1),
                             i % 10 == 0 ? 503 : 200);
    }
    auto rep = analyze(text);
    GQ_CHECK_EQ(rep.lines, 1000u);
    GQ_CHECK_EQ(rep.valid, 1000u);
    GQ_CHECK_EQ(gq::describe(rep.by_status), std::string("200=900 503=100"));
    GQ_CHECK_EQ(gq::describe(rep.top_paths), std::string("/p0=143, /p1=143, /p2=143"));
    GQ_CHECK_EQ(rep.p50_ms, 50u);
    GQ_CHECK_EQ(rep.p95_ms, 95u);
}
