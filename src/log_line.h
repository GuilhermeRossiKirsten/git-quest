// log_line.h — formato das linhas de log e o parser (sem alocações).
//
// Uma linha válida tem exatamente esta forma (campos separados por um espaço):
//
//   2026-10-22T10:00:00Z level=ERROR svc=checkout-api msg="gateway timed out"
//
//   * timestamp: qualquer sequência sem espaços, não vazia;
//   * level: DEBUG, INFO, WARN, ERROR ou FATAL;
//   * svc: um ou mais caracteres em [a-z0-9-];
//   * msg: texto entre aspas duplas (sem aspas dentro), que termina a linha.
//
// Os string_view devolvidos apontam para a própria linha: nada é copiado.
#ifndef GITQUEST_LOG_LINE_H
#define GITQUEST_LOG_LINE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

enum class Level : std::uint8_t { Debug, Info, Warn, Error, Fatal };

inline constexpr std::size_t kLevelCount = 5;

constexpr std::size_t level_index(Level level) noexcept { return static_cast<std::size_t>(level); }

std::optional<Level> parse_level(std::string_view text) noexcept;
std::string_view level_name(Level level) noexcept;

struct ParsedLine {
    std::string_view timestamp;
    Level level = Level::Info;
    std::string_view service;
    std::string_view message;
};

// Preenche `out` e devolve true se a linha estiver no formato acima.
bool parse_line(std::string_view line, ParsedLine& out) noexcept;

#endif
