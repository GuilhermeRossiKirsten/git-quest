// Contrato de tipos da API (só compila; nada é executado).
// Cada static_assert documenta uma decisão de design exigida pelo time.
#include "log_analyzer.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace logan;

// Falhas viram valores: parse_line devolve std::expected, não lança, não usa out-param.
static_assert(std::is_same_v<decltype(parse_line(std::declval<std::string_view>())),
                             std::expected<Record, ParseError>>,
              "parse_line deve retornar std::expected<Record, ParseError>");
static_assert(noexcept(parse_line(std::declval<std::string_view>())),
              "parse_line deve ser noexcept: linha inválida é um valor de erro, não uma exceção");

// Erros têm tipo próprio, sem conversão implícita para int (adeus códigos mágicos).
static_assert(std::is_scoped_enum_v<ErrorKind>,
              "ErrorKind deve ser um enum class (sem conversão implícita para int)");
static_assert(std::is_same_v<decltype(ParseError::kind), ErrorKind>, "ParseError::kind deve ser ErrorKind");
static_assert(std::is_same_v<decltype(ParseError::field), std::size_t>, "ParseError::field deve ser std::size_t");
static_assert(std::is_same_v<decltype(to_string(std::declval<ErrorKind>())), std::string_view>,
              "to_string(ErrorKind) deve retornar std::string_view");
static_assert(noexcept(to_string(std::declval<ErrorKind>())), "to_string(ErrorKind) deve ser noexcept");

// O relatório carrega os erros com número de linha.
static_assert(std::is_same_v<decltype(analyze(std::declval<std::string_view>())), Report>,
              "analyze(std::string_view) deve retornar Report");
static_assert(std::is_same_v<decltype(Report::errors), std::vector<LineError>>,
              "Report::errors deve ser std::vector<LineError>");
static_assert(std::is_same_v<decltype(LineError::line), std::size_t>, "LineError::line deve ser std::size_t");
static_assert(std::is_same_v<decltype(format_report(std::declval<const Report&>())), std::string>,
              "format_report(const Report&) deve retornar std::string");

}  // namespace
