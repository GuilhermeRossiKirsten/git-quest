// Grupo "parse": parse_line, linha a linha.
#include "check.hpp"
#include "support.hpp"

using logan::ErrorKind;
using logan::Method;
using logan::parse_line;

namespace {

constexpr std::string_view kTs = "2026-10-19T12:00:01Z";

// Monta uma linha trocando apenas um campo da linha válida de referência.
std::string line_with(int field, std::string_view value) {
    std::string_view parts[] = {kTs, "GET", "/api/items", "200", "12ms"};
    parts[field - 1] = value;
    return std::format("{} {} {} {} {}", parts[0], parts[1], parts[2], parts[3], parts[4]);
}

std::string expect_error(ErrorKind kind, std::size_t field) {
    return gq::describe(logan::ParseError{kind, field});
}

}  // namespace

GQ_TEST("parse", "linha válida é decodificada campo a campo") {
    auto r = parse_line("2026-10-19T12:00:01Z GET /api/items 200 12ms");
    GQ_CHECK_EQ(gq::describe(r), std::string("ok"));
    if (!r) return;
    GQ_CHECK_EQ(r->timestamp, std::string(kTs));
    GQ_CHECK(r->method == Method::get);
    GQ_CHECK_EQ(r->path, std::string("/api/items"));
    GQ_CHECK_EQ(r->status, 200);
    GQ_CHECK_EQ(r->latency_ms, 12u);
}

GQ_TEST("parse", "todos os métodos suportados") {
    const std::pair<std::string_view, Method> cases[] = {
        {"GET", Method::get},     {"POST", Method::post},   {"PUT", Method::put},
        {"DELETE", Method::del},  {"PATCH", Method::patch}, {"HEAD", Method::head},
    };
    for (auto [text, method] : cases) {
        auto r = parse_line(line_with(2, text));
        GQ_CHECK_EQ(gq::describe(r), std::string("ok"));
        if (r) GQ_CHECK(r->method == method);
    }
}

GQ_TEST("parse", "espaços repetidos, tabs e espaços nas pontas separam campos") {
    auto r = parse_line("  2026-10-19T12:00:01Z \t POST\t/login   201  7ms  ");
    GQ_CHECK_EQ(gq::describe(r), std::string("ok"));
    if (!r) return;
    GQ_CHECK_EQ(r->path, std::string("/login"));
    GQ_CHECK_EQ(r->status, 201);
    GQ_CHECK_EQ(r->latency_ms, 7u);
}

GQ_TEST("parse", "CRLF: o '\\r' final é ignorado") {
    auto r = parse_line("2026-10-19T12:00:01Z GET /api/items 200 12ms\r");
    GQ_CHECK_EQ(gq::describe(r), std::string("ok"));
    if (r) GQ_CHECK_EQ(r->latency_ms, 12u);
}

GQ_TEST("parse", "linha vazia ou em branco -> empty (campo 0)") {
    for (std::string_view text : {"", "   ", "\t", "\r", " \t \r"}) {
        GQ_CHECK_EQ(gq::describe(parse_line(text)), expect_error(ErrorKind::empty, 0));
    }
}

GQ_TEST("parse", "campos faltando apontam o primeiro campo ausente") {
    GQ_CHECK_EQ(gq::describe(parse_line("2026-10-19T12:00:01Z GET /api/items 200")),
                expect_error(ErrorKind::missing_fields, 5));
    GQ_CHECK_EQ(gq::describe(parse_line("2026-10-19T12:00:01Z GET")),
                expect_error(ErrorKind::missing_fields, 3));
    GQ_CHECK_EQ(gq::describe(parse_line("2026-10-19T12:00:01Z")),
                expect_error(ErrorKind::missing_fields, 2));
}

GQ_TEST("parse", "campos extras são rejeitados (campo 6)") {
    GQ_CHECK_EQ(gq::describe(parse_line("2026-10-19T12:00:01Z GET /api/items 200 12ms extra")),
                expect_error(ErrorKind::extra_fields, 6));
    GQ_CHECK_EQ(gq::describe(parse_line("2026-10-19T12:00:01Z GET /api/items 200 12ms 13ms\r")),
                expect_error(ErrorKind::extra_fields, 6));
}

GQ_TEST("parse", "timestamp precisa ser AAAA-MM-DDTHH:MM:SSZ com valores válidos") {
    for (std::string_view ts : {"2026-13-19T12:00:01Z", "2026-00-19T12:00:01Z", "2026-10-32T12:00:01Z",
                                "2026-10-00T12:00:01Z", "2026-10-19T24:00:01Z", "2026-10-19T12:60:01Z",
                                "2026-10-19T12:00:60Z", "2026-1O-19T12:00:01Z", "2026-10-19T12:00:01",
                                "2026-10-19X12:00:01Z", "2026/10/19T12:00:01Z", "26-10-19T12:00:01Z",
                                "2026-10-19T12:00:01ZZ", "+026-10-19T12:00:01Z"}) {
        GQ_CHECK_EQ(gq::describe(parse_line(line_with(1, ts))), expect_error(ErrorKind::bad_timestamp, 1));
    }
    GQ_CHECK_EQ(gq::describe(parse_line(line_with(1, "2026-12-31T23:59:59Z"))), std::string("ok"));
    GQ_CHECK_EQ(gq::describe(parse_line(line_with(1, "2026-01-01T00:00:00Z"))), std::string("ok"));
}

GQ_TEST("parse", "método desconhecido ou em minúsculas -> bad_method") {
    for (std::string_view m : {"get", "Get", "FETCH", "GETS", "-"}) {
        GQ_CHECK_EQ(gq::describe(parse_line(line_with(2, m))), expect_error(ErrorKind::bad_method, 2));
    }
}

GQ_TEST("parse", "path deve começar com '/' e não ter caracteres de controle") {
    GQ_CHECK_EQ(gq::describe(parse_line(line_with(3, "api/items"))), expect_error(ErrorKind::bad_path, 3));
    GQ_CHECK_EQ(gq::describe(parse_line(line_with(3, "/api\x01items"))), expect_error(ErrorKind::bad_path, 3));
    GQ_CHECK_EQ(gq::describe(parse_line(line_with(3, "/api\x7Fitems"))), expect_error(ErrorKind::bad_path, 3));
    GQ_CHECK_EQ(gq::describe(parse_line(line_with(3, "/"))), std::string("ok"));
}

GQ_TEST("parse", "path com UTF-8 é aceito byte a byte") {
    for (std::string_view path : {"/caf\xC3\xA9/menu", "/busca?q=a\xC3\xA7\xC3\xA3o", "/emoji/\xF0\x9F\x9A\x80"}) {
        auto r = parse_line(line_with(3, path));
        GQ_CHECK_EQ(gq::describe(r), std::string("ok"));
        if (r) GQ_CHECK_EQ(r->path, std::string(path));
    }
}

GQ_TEST("parse", "status precisa ter 3 dígitos entre 100 e 599") {
    for (std::string_view s : {"999", "600", "099", "0", "20", "1000", "2OO", "200x", "-200", "+200"}) {
        GQ_CHECK_EQ(gq::describe(parse_line(line_with(4, s))), expect_error(ErrorKind::bad_status, 4));
    }
    for (auto [text, value] : {std::pair{"100", 100}, std::pair{"404", 404}, std::pair{"599", 599}}) {
        auto r = parse_line(line_with(4, text));
        GQ_CHECK_EQ(gq::describe(r), std::string("ok"));
        if (r) GQ_CHECK_EQ(r->status, value);
    }
}

GQ_TEST("parse", "latência é um inteiro não negativo seguido de 'ms'") {
    for (std::string_view l : {"-5ms", "12", "ms", "1.5ms", "12MS", "+5ms", "12ms5", "4294967296ms",
                               "99999999999999999999ms", "0x1fms", "abcms"}) {
        GQ_CHECK_EQ(gq::describe(parse_line(line_with(5, l))), expect_error(ErrorKind::bad_latency, 5));
    }
    for (auto [text, value] : {std::pair{"0ms", 0u}, std::pair{"007ms", 7u}, std::pair{"4294967295ms", 4294967295u}}) {
        auto r = parse_line(line_with(5, text));
        GQ_CHECK_EQ(gq::describe(r), std::string("ok"));
        if (r) GQ_CHECK_EQ(r->latency_ms, value);
    }
}

GQ_TEST("parse", "o primeiro campo inválido (da esquerda) é o reportado") {
    GQ_CHECK_EQ(gq::describe(parse_line("2026-13-19T12:00:01Z get x 999 -1ms")),
                expect_error(ErrorKind::bad_timestamp, 1));
    GQ_CHECK_EQ(gq::describe(parse_line("2026-10-19T12:00:01Z GET /x 999 -1ms")),
                expect_error(ErrorKind::bad_status, 4));
}

GQ_TEST("parse", "to_string dá nomes estáveis a cada ErrorKind") {
    using logan::to_string;
    GQ_CHECK_EQ(to_string(ErrorKind::empty), std::string_view("empty"));
    GQ_CHECK_EQ(to_string(ErrorKind::missing_fields), std::string_view("missing_fields"));
    GQ_CHECK_EQ(to_string(ErrorKind::extra_fields), std::string_view("extra_fields"));
    GQ_CHECK_EQ(to_string(ErrorKind::bad_timestamp), std::string_view("bad_timestamp"));
    GQ_CHECK_EQ(to_string(ErrorKind::bad_method), std::string_view("bad_method"));
    GQ_CHECK_EQ(to_string(ErrorKind::bad_path), std::string_view("bad_path"));
    GQ_CHECK_EQ(to_string(ErrorKind::bad_status), std::string_view("bad_status"));
    GQ_CHECK_EQ(to_string(ErrorKind::bad_latency), std::string_view("bad_latency"));
}
