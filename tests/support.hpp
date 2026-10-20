// support.hpp — utilitários dos testes: descrevem resultados como texto para
// que as mensagens de falha mostrem "obtido" x "esperado" de forma legível.
#pragma once

#include "log_analyzer.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace gq {

extern std::size_t g_exceptions_allocated;

// Log de referência: 8 linhas, uma em branco e duas inválidas (linhas 4 e 7).
inline constexpr std::string_view kSampleLog =
    "2026-10-19T12:00:01Z GET /api/items 200 12ms\n"
    "2026-10-19T12:00:02Z GET /api/items 200 30ms\n"
    "\n"
    "2026-10-19T12:00:03Z POST /login 2OO 5ms\n"
    "2026-10-19T12:00:04Z GET /health 200 1ms\n"
    "2026-10-19T12:00:05Z DELETE /api/items/7 404 250ms\n"
    "2026-10-19T12:00:06Z GET /api/items 500 -3ms\n"
    "2026-10-19T12:00:07Z PUT /api/items/7 204 40ms\n";

inline constexpr std::string_view kSampleReport =
    "linhas: 8 | válidas: 5 | inválidas: 2 | em branco: 1\n"
    "status: 200=3 204=1 404=1\n"
    "latência: p50=30ms p95=250ms\n"
    "top: /api/items (2), /api/items/7 (2), /health (1)\n"
    "erros:\n"
    "  linha 4: bad_status (campo 4)\n"
    "  linha 7: bad_latency (campo 5)\n";

// Converte "\n" em "\r\n" (log gerado no Windows).
inline std::string to_crlf(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}

// Linha válida (com '\n') para o path, a latência e o status dados.
inline std::string log_line(std::string_view path, std::uint32_t latency_ms, int status = 200) {
    return std::format("2026-10-19T12:00:00Z GET {} {} {}ms\n", path, status, latency_ms);
}

// Nomes próprios dos testes (não dependem de logan::to_string).
inline std::string_view kind_name(logan::ErrorKind kind) {
    switch (kind) {
    case logan::ErrorKind::empty: return "empty";
    case logan::ErrorKind::missing_fields: return "missing_fields";
    case logan::ErrorKind::extra_fields: return "extra_fields";
    case logan::ErrorKind::bad_timestamp: return "bad_timestamp";
    case logan::ErrorKind::bad_method: return "bad_method";
    case logan::ErrorKind::bad_path: return "bad_path";
    case logan::ErrorKind::bad_status: return "bad_status";
    case logan::ErrorKind::bad_latency: return "bad_latency";
    }
    return "?";
}

inline std::string describe(const logan::ParseError& e) {
    return std::format("{}@campo {}", kind_name(e.kind), e.field);
}

inline std::string describe(const std::expected<logan::Record, logan::ParseError>& r) {
    return r ? std::string("ok") : describe(r.error());
}

inline std::string describe(const std::vector<logan::LineError>& errors) {
    std::string out;
    for (const auto& e : errors) {
        out += std::format("{}linha {}: {}", out.empty() ? "" : "; ", e.line, describe(e.error));
    }
    return out;
}

inline std::string describe(const std::vector<logan::PathHits>& top) {
    std::string out;
    for (const auto& p : top) {
        out += std::format("{}{}={}", out.empty() ? "" : ", ", p.path, p.hits);
    }
    return out;
}

inline std::string describe(const std::map<int, std::size_t>& by_status) {
    std::string out;
    for (const auto& [status, count] : by_status) {
        out += std::format("{}{}={}", out.empty() ? "" : " ", status, count);
    }
    return out;
}

}  // namespace gq
