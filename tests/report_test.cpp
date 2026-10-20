// Grupo "report": formato exato do relatório textual.
#include "check.hpp"
#include "support.hpp"

using logan::ErrorKind;
using logan::format_report;
using logan::Report;

GQ_TEST("report", "relatório montado à mão: formato exato") {
    Report rep;
    rep.lines = 8;
    rep.valid = 5;
    rep.blank = 1;
    rep.by_status = {{200, 3}, {404, 1}, {204, 1}};
    rep.p50_ms = 30;
    rep.p95_ms = 250;
    rep.top_paths = {{"/api/items", 2}, {"/api/items/7", 2}, {"/health", 1}};
    rep.errors = {{4, {ErrorKind::bad_status, 4}}, {7, {ErrorKind::bad_latency, 5}}};
    GQ_CHECK_EQ(format_report(rep), std::string(gq::kSampleReport));
}

GQ_TEST("report", "relatório vazio usa '-' e 'nenhum'") {
    GQ_CHECK_EQ(format_report(Report{}), std::string("linhas: 0 | válidas: 0 | inválidas: 0 | em branco: 0\n"
                                                     "status: -\n"
                                                     "latência: -\n"
                                                     "top: -\n"
                                                     "erros: nenhum\n"));
}

GQ_TEST("report", "só linhas inválidas: sem estatísticas, todos os erros listados") {
    Report rep;
    rep.lines = 3;
    rep.blank = 1;
    rep.errors = {{1, {ErrorKind::extra_fields, 6}}, {3, {ErrorKind::bad_timestamp, 1}}};
    GQ_CHECK_EQ(format_report(rep), std::string("linhas: 3 | válidas: 0 | inválidas: 2 | em branco: 1\n"
                                                "status: -\n"
                                                "latência: -\n"
                                                "top: -\n"
                                                "erros:\n"
                                                "  linha 1: extra_fields (campo 6)\n"
                                                "  linha 3: bad_timestamp (campo 1)\n"));
}

GQ_TEST("report", "sem erros: 'erros: nenhum'") {
    Report rep;
    rep.lines = 1;
    rep.valid = 1;
    rep.by_status = {{301, 1}};
    rep.p50_ms = 0;
    rep.p95_ms = 0;
    rep.top_paths = {{"/caf\xC3\xA9", 1}};
    GQ_CHECK_EQ(format_report(rep), std::string("linhas: 1 | válidas: 1 | inválidas: 0 | em branco: 0\n"
                                                "status: 301=1\n"
                                                "latência: p50=0ms p95=0ms\n"
                                                "top: /caf\xC3\xA9 (1)\n"
                                                "erros: nenhum\n"));
}

GQ_TEST("report", "ponta a ponta: format_report(analyze(log))") {
    GQ_CHECK_EQ(format_report(logan::analyze(gq::kSampleLog)), std::string(gq::kSampleReport));
    GQ_CHECK_EQ(format_report(logan::analyze(gq::to_crlf(gq::kSampleLog))), std::string(gq::kSampleReport));
}
