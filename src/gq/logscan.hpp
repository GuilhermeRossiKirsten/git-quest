#pragma once
// logscan — leitor de logs do time de plataforma.
//
// As ASSINATURAS abaixo são o contrato público usado pelos testes e pelo
// relatório: os nomes e os tipos de retorno (std::generator) não podem mudar.
// O conteúdo das structs e a implementação (src/logscan.cpp) são seus.
//
// Formato de um registro (campos separados por um ou mais espaços):
//
//   2026-10-20T08:00:01Z ERROR payments 125 cartão recusado pelo emissor
//   <timestamp>          <nível> <serviço> <latência ms> <mensagem até o fim da linha>
//
// Linhas vazias e linhas que começam com '#' são ignoradas por records().

#include <cstddef>
#include <generator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "gq/source.hpp"
#include "gq/vfs.hpp"

namespace gq {

// Tamanho do buffer de leitura usado por lines().
inline constexpr std::size_t kChunkSize = 4096;

// Linha que não segue o formato de registro. what() cita o número da linha
// (contando a partir de 1, inclusive linhas vazias e comentários).
class parse_error : public std::runtime_error {
 public:
  parse_error(std::size_t line_number, const std::string& detail)
      : std::runtime_error("linha " + std::to_string(line_number) + ": " + detail),
        line_number_(line_number) {}
  std::size_t line_number() const noexcept { return line_number_; }

 private:
  std::size_t line_number_;
};

// Registro já interpretado. Os campos de texto apontam direto para a linha lida,
// sem cópias: o leitor processa gigabytes e cada alocação conta.
struct Record {
  std::string_view timestamp;
  std::string_view level;
  std::string_view service;
  int latency_ms = 0;
  std::string_view message;
};

// Interpreta uma linha de registro; lança parse_error(line_number, ...) se a
// linha estiver malformada.
Record parse_record(std::string_view line, std::size_t line_number);

// Abre `src` e produz cada linha, sem o '\n' (e sem um '\r' final). A última
// linha é produzida mesmo sem '\n'. A fonte é lida em blocos de kChunkSize.
std::generator<std::string_view> lines(Source& src);

// Produz os registros de `src` (pula linhas vazias e comentários).
std::generator<Record> records(Source& src);

// Os primeiros `limit` registros cujo nível é `level`, na ordem do arquivo.
std::vector<Record> first_matching(Source& src, std::string_view level, std::size_t limit);

// Caminhos completos ("raiz/sub/arquivo") de todos os arquivos sob `root`, em
// pré-ordem: primeiro os arquivos do diretório, depois cada subdiretório, na
// ordem em que aparecem.
std::generator<std::string> walk(const Dir& root);

}  // namespace gq
