// log_analyzer.hpp — API pública do analisador de logs de acesso.
//
// Os nomes, tipos e membros declarados aqui são usados pelos testes e pelo
// time de observabilidade: não os renomeie. O contrato completo (inclusive o
// que ainda falta cumprir) está no README, seção "Problema".
//
// Formato de uma linha de log (campos separados por espaços ou tabs):
//
//     2026-10-19T12:00:01Z GET /api/items 200 12ms
//     <timestamp>          <método> <path> <status> <latência>
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace logan {

enum class Method { get, post, put, del, patch, head };

struct Record {
    std::string timestamp;      // "2026-10-19T12:00:01Z"
    Method method{};
    std::string path;           // bytes exatos da linha (UTF-8 permitido)
    int status = 0;             // 100..599
    std::uint32_t latency_ms = 0;
};

// Motivo pelo qual uma linha foi rejeitada.
enum ErrorKind {
    empty,           // linha vazia ou só com espaços
    missing_fields,  // menos de 5 campos
    extra_fields,    // mais de 5 campos
    bad_timestamp,
    bad_method,
    bad_path,
    bad_status,
    bad_latency,
};

struct ParseError {
    ErrorKind kind{};
    std::size_t field = 0;  // campo culpado, 1-based (0 = a linha inteira)

    bool operator==(const ParseError&) const = default;
};

struct LineError {
    std::size_t line = 0;   // número da linha no texto, 1-based
    ParseError error;

    bool operator==(const LineError&) const = default;
};

struct PathHits {
    std::string path;
    std::size_t hits = 0;

    bool operator==(const PathHits&) const = default;
};

struct Report {
    std::size_t lines = 0;    // linhas do texto (um '\n' final não abre linha nova)
    std::size_t valid = 0;    // linhas aceitas
    std::size_t blank = 0;    // linhas em branco (ignoradas, mas contadas)
    std::map<int, std::size_t> by_status;  // status -> quantidade
    std::uint32_t p50_ms = 0;  // percentis de latência (nearest-rank)
    std::uint32_t p95_ms = 0;
    std::vector<PathHits> top_paths;  // até 3 paths mais acessados
    std::vector<LineError> errors;    // linhas rejeitadas, em ordem
};

// Nome estável do erro, usado no relatório ("bad_status", ...).
std::string_view to_string(ErrorKind kind) noexcept;

// Interpreta UMA linha (sem o '\n').
std::expected<Record, ParseError> parse_line(std::string_view line);

// Analisa um texto com várias linhas.
Report analyze(std::string_view text);

// Relatório textual consumido pelo painel do plantão.
std::string format_report(const Report& report);

}  // namespace logan
