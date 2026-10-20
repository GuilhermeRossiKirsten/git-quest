// Implementação do logscan. Veja o contrato em src/gq/logscan.hpp.
#include "gq/logscan.hpp"

#include <array>
#include <charconv>
#include <cstdio>
#include <utility>

namespace gq {

namespace {

// Separa o próximo campo de `rest` (pulando espaços à esquerda).
std::string_view next_field(std::string_view& rest) {
  const auto begin = rest.find_first_not_of(' ');
  if (begin == std::string_view::npos) {
    rest = {};
    return {};
  }
  rest.remove_prefix(begin);
  const auto end = rest.find(' ');
  const std::string_view field = rest.substr(0, end);
  rest.remove_prefix(end == std::string_view::npos ? rest.size() : end);
  return field;
}

}  // namespace

Record parse_record(std::string_view line, std::size_t line_number) {
  std::string_view rest = line;
  Record rec;
  rec.timestamp = next_field(rest);
  rec.level = next_field(rest);
  rec.service = next_field(rest);
  const std::string_view latency = next_field(rest);
  if (latency.empty()) throw parse_error(line_number, "registro incompleto");
  const auto [ptr, ec] = std::from_chars(latency.data(), latency.data() + latency.size(), rec.latency_ms);
  if (ec != std::errc{} || ptr != latency.data() + latency.size() || rec.latency_ms < 0) {
    throw parse_error(line_number, "latência inválida '" + std::string(latency) + "'");
  }
  const auto msg = rest.find_first_not_of(' ');
  rec.message = msg == std::string_view::npos ? std::string_view{} : rest.substr(msg);
  return rec;
}

std::generator<std::string_view> lines(Source& src) {
  // Carrega tudo de uma vez: dividir em linhas fica simples e a leitura sai
  // num laço só, sem idas e vindas entre leitor e consumidor.
  std::vector<std::string> all;
  src.open();
  try {
    std::array<char, kChunkSize> buffer{};
    std::string pending;
    while (const std::size_t n = src.read(buffer)) {
      pending.append(buffer.data(), n);
      std::size_t start = 0;
      for (std::size_t nl; (nl = pending.find('\n', start)) != std::string::npos; start = nl + 1) {
        all.emplace_back(pending, start, nl - start);
      }
      pending.erase(0, start);
    }
    if (!pending.empty()) all.push_back(std::move(pending));
  } catch (const io_error& e) {
    // Um disco de rede às vezes falha no fim do arquivo; o que já foi lido
    // ainda serve para o relatório.
    std::fprintf(stderr, "logscan: aviso: leitura interrompida (%s); usando dados parciais\n", e.what());
  }
  src.close();

  for (const std::string& line : all) {
    std::string_view view = line;
    if (view.ends_with('\r')) view.remove_suffix(1);
    co_yield view;
  }
}

std::generator<Record> records(Source& src) {
  std::size_t line_number = 0;
  for (std::string_view line : lines(src)) {
    ++line_number;
    if (line.empty() || line.starts_with('#')) continue;
    try {
      co_yield parse_record(line, line_number);
    } catch (const parse_error& e) {
      // Linha corrompida não pode derrubar o relatório inteiro.
      std::fprintf(stderr, "logscan: aviso: %s (ignorada)\n", e.what());
    }
  }
}

std::vector<Record> first_matching(Source& src, std::string_view level, std::size_t limit) {
  std::vector<Record> all;
  for (const Record& rec : records(src)) all.push_back(rec);

  std::vector<Record> found;
  for (const Record& rec : all) {
    if (found.size() == limit) break;
    if (rec.level == level) found.push_back(rec);
  }
  return found;
}

namespace {

std::generator<std::string> walk_dir(Dir dir, std::string prefix) {
  for (const File& file : dir.files) co_yield prefix + "/" + file.name;
  for (Dir sub : dir.subdirs) {
    for (std::string path : walk_dir(sub, prefix + "/" + sub.name)) co_yield path;
  }
}

}  // namespace

std::generator<std::string> walk(const Dir& root) {
  for (std::string path : walk_dir(root, root.name)) co_yield path;
}

}  // namespace gq
