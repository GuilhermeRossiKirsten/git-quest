// Testes de comportamento do logscan (um processo por teste, sob ASan/UBSan).
#include <chrono>
#include <cstddef>
#include <generator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "gq/logscan.hpp"
#include "support/minitest.hpp"

namespace {

using gq::MemorySource;

// Linha de registro número `i` (ERROR quando i % 5 == 2).
std::string log_line(std::size_t i) {
  const char* level = i % 5 == 2 ? "ERROR" : "INFO";
  return "2026-10-20T08:00:" + std::to_string(i % 60) + "Z " + level + " servico-de-pagamentos-" +
         std::to_string(i % 3) + " " + std::to_string(i % 1000) + " mensagem numero " +
         std::to_string(i) + " com texto suficiente para sair do SSO";
}

std::string make_log(std::size_t count) {
  std::string text;
  for (std::size_t i = 0; i < count; ++i) text += log_line(i) + "\n";
  return text;
}

// Bytes das `n` primeiras linhas de `text`, contando os '\n'.
std::size_t prefix_bytes(const std::string& text, std::size_t n) {
  std::size_t pos = 0;
  for (std::size_t i = 0; i < n; ++i) pos = text.find('\n', pos) + 1;
  return pos;
}

std::vector<std::string> collect_lines(MemorySource& src) {
  std::vector<std::string> out;
  for (std::string_view line : gq::lines(src)) out.emplace_back(line);
  return out;
}

}  // namespace

// ---------------------------------------------------------------- toolchain

TEST(toolchain_generator, "toolchain: C++23 com std::generator (__cpp_lib_generator) e coroutines") {
#if defined(__cpp_lib_generator) && defined(__cpp_impl_coroutine)
  CHECK(__cpp_lib_generator >= 202207L);
  CHECK(__cpp_impl_coroutine >= 201902L);
#else
  ::minitest::fail(__LINE__, "a biblioteca padrão não oferece std::generator (exige g++ >= 14 e -std=c++23)");
#endif
}

// ---------------------------------------------------------------- lines()

TEST(lines_divide_corretamente, "lines(): divide linhas, remove \\r, aceita linha vazia e última linha sem \\n") {
  MemorySource src("alfa\r\n\nbeta gama\ndelta", 3);
  const std::vector<std::string> expected{"alfa", "", "beta gama", "delta"};
  CHECK_EQ(collect_lines(src), expected);

  MemorySource empty("");
  CHECK(collect_lines(empty).empty());
}

TEST(lines_atravessa_blocos, "lines(): linhas maiores que um bloco de leitura chegam inteiras") {
  const std::string big(3 * gq::kChunkSize + 17, 'x');
  MemorySource src("curta\n" + big + "\nfim\n", 1000);
  const auto got = collect_lines(src);
  CHECK_EQ(got.size(), std::size_t{3});
  if (got.size() == 3) {
    CHECK_EQ(got[0], std::string("curta"));
    CHECK_EQ(got[1].size(), big.size());
    CHECK(got[1] == big);
    CHECK_EQ(got[2], std::string("fim"));
  }
}

TEST(lines_preguicoso, "lines(): ler 10 linhas de ~2 MiB lê só o necessário da fonte") {
  const std::string text = make_log(20'000);
  MemorySource src(text);
  auto gen = gq::lines(src);
  CHECK_EQ(src.stats().opens, std::size_t{0});  // nada acontece antes de iterar
  std::size_t taken = 0;
  for (std::string_view line : gen) {
    CHECK_EQ(std::string(line), log_line(taken));
    if (++taken == 10) break;
  }
  const std::size_t needed = prefix_bytes(text, 10);
  if (src.stats().bytes_read > needed + 2 * gq::kChunkSize) {
    ::minitest::fail(__LINE__, "para entregar 10 linhas (" + std::to_string(needed) + " bytes) foram lidos " +
                                   std::to_string(src.stats().bytes_read) + " bytes de " +
                                   std::to_string(text.size()) + " — o leitor não é preguiçoso");
  }
}

TEST(lines_fecha_ao_parar, "lines(): destruir o generator no meio da leitura fecha a fonte") {
  const std::string text = make_log(20'000);
  MemorySource src(text);
  {
    auto gen = gq::lines(src);
    auto it = gen.begin();
    ++it;
    ++it;
    CHECK_EQ(std::string(*it), log_line(2));
    CHECK(src.is_open());  // a fonte continua aberta enquanto o consumidor ainda pode pedir mais
  }
  CHECK(!src.is_open());
  CHECK_EQ(src.stats().opens, std::size_t{1});
  CHECK_EQ(src.stats().closes, std::size_t{1});
}

TEST(lines_fecha_no_fim, "lines(): consumir tudo fecha a fonte exatamente uma vez") {
  MemorySource src(make_log(50), 100);
  CHECK_EQ(collect_lines(src).size(), std::size_t{50});
  CHECK(!src.is_open());
  CHECK_EQ(src.stats().closes, std::size_t{1});
}

TEST(lines_erro_de_leitura, "lines(): falha de leitura chega ao consumidor como gq::io_error, depois das linhas já lidas") {
  const std::string text = make_log(2'000);
  const std::size_t fail_offset = prefix_bytes(text, 1'200) + 10;  // no meio da linha 1200
  MemorySource src(text);
  src.fail_at(fail_offset, "EIO simulado");
  std::size_t delivered = 0;
  bool thrown = false;
  try {
    for (std::string_view line : gq::lines(src)) {
      if (std::string(line) != log_line(delivered)) {
        ::minitest::fail(__LINE__, "linha " + std::to_string(delivered) + " chegou corrompida");
        return;
      }
      ++delivered;
    }
  } catch (const gq::io_error& e) {
    thrown = true;
    CHECK_EQ(std::string(e.what()), std::string("EIO simulado"));
  }
  if (!thrown) ::minitest::fail(__LINE__, "a falha de leitura foi engolida: o consumidor recebeu " +
                                              std::to_string(delivered) + " linhas e nenhum erro");
  CHECK_EQ(delivered, std::size_t{1'200});  // todas as linhas completas antes da falha
  CHECK(!src.is_open());
  CHECK_EQ(src.stats().closes, std::size_t{1});
}

// ---------------------------------------------------------------- records()

TEST(records_interpreta, "records(): interpreta campos e pula linhas vazias e comentários") {
  MemorySource src(
      "# cabeçalho\n"
      "2026-10-20T08:00:01Z ERROR payments 125 cartão recusado  pelo emissor\n"
      "\n"
      "2026-10-20T08:00:02Z   INFO auth 7\n",
      11);
  std::vector<gq::Record> got;
  for (auto&& rec : gq::records(src)) got.push_back(std::move(rec));
  CHECK_EQ(got.size(), std::size_t{2});
  if (got.size() != 2) return;
  CHECK_EQ(std::string(got[0].timestamp), std::string("2026-10-20T08:00:01Z"));
  CHECK_EQ(std::string(got[0].level), std::string("ERROR"));
  CHECK_EQ(std::string(got[0].service), std::string("payments"));
  CHECK_EQ(got[0].latency_ms, 125);
  CHECK_EQ(std::string(got[0].message), std::string("cartão recusado  pelo emissor"));
  CHECK_EQ(std::string(got[1].level), std::string("INFO"));
  CHECK_EQ(got[1].latency_ms, 7);
  CHECK(got[1].message.empty());
}

TEST(records_sobrevivem, "records(): registros coletados continuam válidos depois que o generator acaba") {
  const std::string text = make_log(500);
  std::vector<gq::Record> got;
  {
    MemorySource src(text, 64);
    got = gq::records(src) | std::views::take(40) | std::ranges::to<std::vector>();
  }
  CHECK_EQ(got.size(), std::size_t{40});
  for (std::size_t i = 0; i < got.size(); ++i) {
    const std::string expected_service = "servico-de-pagamentos-" + std::to_string(i % 3);
    const std::string expected_message = "mensagem numero " + std::to_string(i) + " com texto suficiente para sair do SSO";
    if (std::string(got[i].service) != expected_service || std::string(got[i].message) != expected_message) {
      ::minitest::fail(__LINE__, "registro " + std::to_string(i) + " corrompido: serviço='" +
                                     std::string(got[i].service) + "'");
      return;
    }
  }
}

TEST(records_erro_de_parse, "records(): linha malformada lança gq::parse_error no consumo, citando a linha") {
  MemorySource src(
      "2026-10-20T08:00:01Z INFO auth 1 ok\n"
      "# comentário\n"
      "2026-10-20T08:00:02Z INFO auth 2 ok\n"
      "2026-10-20T08:00:03Z INFO auth latencia-quebrada ok\n"
      "2026-10-20T08:00:04Z INFO auth 4 ok\n",
      16);
  std::size_t delivered = 0;
  bool thrown = false;
  try {
    for (auto&& rec : gq::records(src)) {
      static_cast<void>(rec);
      ++delivered;
    }
  } catch (const gq::parse_error& e) {
    thrown = true;
    CHECK_EQ(e.line_number(), std::size_t{4});
    CHECK(std::string_view(e.what()).starts_with("linha 4"));
  }
  if (!thrown) ::minitest::fail(__LINE__, "a linha 4 está malformada, mas nenhum gq::parse_error chegou ao consumidor");
  CHECK_EQ(delivered, std::size_t{2});
  CHECK(!src.is_open());
}

TEST(records_erro_de_leitura, "records(): gq::io_error da fonte atravessa records() até o consumidor") {
  const std::string text = make_log(300);
  MemorySource src(text, 128);
  src.fail_at(prefix_bytes(text, 100), "conexão perdida");
  std::size_t delivered = 0;
  bool thrown = false;
  try {
    for (auto&& rec : gq::records(src)) {
      static_cast<void>(rec);
      ++delivered;
    }
  } catch (const gq::io_error&) {
    thrown = true;
  }
  if (!thrown) ::minitest::fail(__LINE__, "a falha de leitura não chegou ao consumidor de records()");
  CHECK_EQ(delivered, std::size_t{100});
  CHECK(!src.is_open());
}

// ---------------------------------------------------------------- pipeline

TEST(pipeline_composto, "records() compõe com views::filter | views::take sem ler o arquivo inteiro") {
  const std::string text = make_log(20'000);
  MemorySource src(text);
  std::vector<int> latencies;
  for (auto&& rec : gq::records(src) | std::views::filter([](const gq::Record& r) { return r.level == "ERROR"; }) |
                        std::views::take(3)) {
    latencies.push_back(rec.latency_ms);
  }
  CHECK_EQ(latencies, (std::vector<int>{2, 7, 12}));
  // O 3º ERROR está na linha 13.
  if (src.stats().bytes_read > prefix_bytes(text, 13) + 2 * gq::kChunkSize) {
    ::minitest::fail(__LINE__, "foram lidos " + std::to_string(src.stats().bytes_read) +
                                   " bytes para achar 3 erros nas 13 primeiras linhas");
  }
}

TEST(first_matching_preguicoso, "first_matching(): devolve os N primeiros e lê só até o N-ésimo") {
  const std::string text = make_log(20'000);
  MemorySource src(text);
  const std::vector<gq::Record> got = gq::first_matching(src, "ERROR", 10);
  CHECK_EQ(got.size(), std::size_t{10});
  for (std::size_t k = 0; k < got.size(); ++k) {
    const std::size_t i = 5 * k + 2;
    CHECK_EQ(std::string(got[k].level), std::string("ERROR"));
    CHECK_EQ(std::string(got[k].message),
             "mensagem numero " + std::to_string(i) + " com texto suficiente para sair do SSO");
  }
  const std::size_t needed = prefix_bytes(text, 5 * 9 + 3);
  if (src.stats().bytes_read > needed + 2 * gq::kChunkSize) {
    ::minitest::fail(__LINE__, "para achar 10 erros (" + std::to_string(needed) + " bytes) foram lidos " +
                                   std::to_string(src.stats().bytes_read) + " de " + std::to_string(text.size()));
  }
  CHECK(!src.is_open());
  CHECK_EQ(src.stats().closes, std::size_t{1});
}

TEST(first_matching_bordas, "first_matching(): limite 0, nível inexistente e menos ocorrências que o limite") {
  const std::string text = make_log(30);
  {
    MemorySource src(text);
    CHECK(gq::first_matching(src, "ERROR", 0).empty());
    CHECK(!src.is_open());
  }
  {
    MemorySource src(text);
    CHECK(gq::first_matching(src, "FATAL", 5).empty());
    CHECK_EQ(src.stats().bytes_read, text.size());
  }
  {
    MemorySource src(text);
    CHECK_EQ(gq::first_matching(src, "ERROR", 100).size(), std::size_t{6});
  }
}

// ---------------------------------------------------------------- walk()

TEST(walk_ordem, "walk(): caminhos completos em pré-ordem (arquivos, depois subdiretórios)") {
  gq::Dir root("logs");
  root.add_file("a.log");
  gq::Dir& eu = root.add_dir("eu");
  eu.add_file("b.log");
  eu.add_dir("vazio");
  eu.add_dir("x").add_file("c.log");
  root.add_file("z.log");
  root.add_dir("us").add_file("d.log");
  std::vector<std::string> got;
  for (auto&& path : gq::walk(root)) got.push_back(std::move(path));
  const std::vector<std::string> expected{"logs/a.log", "logs/z.log", "logs/eu/b.log", "logs/eu/x/c.log",
                                          "logs/us/d.log"};
  CHECK_EQ(got, expected);

  gq::Dir lonely("nada");
  std::size_t n = 0;
  for (auto&& path : gq::walk(lonely)) n += path.size();
  CHECK_EQ(n, std::size_t{0});
}

TEST(walk_sem_copias, "walk(): percorre a árvore sem copiar nenhum diretório") {
  gq::Dir root("r");
  for (int i = 0; i < 20; ++i) {
    gq::Dir& a = root.add_dir("a" + std::to_string(i));
    for (int j = 0; j < 20; ++j) {
      gq::Dir& b = a.add_dir("b" + std::to_string(j));
      for (int k = 0; k < 5; ++k) b.add_file("f" + std::to_string(k));
    }
  }
  gq::Dir::reset_copies();
  std::size_t files = 0;
  for (auto&& path : gq::walk(root)) files += path.empty() ? 0 : 1;
  CHECK_EQ(files, std::size_t{2000});
  if (gq::Dir::copies() != 0) {
    ::minitest::fail(__LINE__, std::to_string(gq::Dir::copies()) + " diretórios copiados para listar a árvore");
  }
}

TEST(walk_profundo, "walk(): árvore com 4000 níveis é listada corretamente, sem estouro de pilha") {
  constexpr int kDepth = 4000;
  gq::Dir root("r");
  gq::Dir* current = &root;
  std::string expected = "r";
  for (int i = 0; i < kDepth; ++i) {
    current = &current->add_dir("d");
    expected += "/d";
  }
  current->add_file("fundo.log");
  expected += "/fundo.log";
  gq::Dir::reset_copies();
  std::vector<std::string> got;
  for (auto&& path : gq::walk(root)) got.push_back(std::move(path));
  CHECK_EQ(got.size(), std::size_t{1});
  if (got.size() == 1) CHECK(got[0] == expected);
  CHECK_EQ(gq::Dir::copies(), std::size_t{0});
}

namespace {

// Tempo (s) para produzir os arquivos do diretório mais fundo, excluindo a
// descida até ele. Devolve -1 se passar de `budget` segundos.
double time_bottom_files(const gq::Dir& root, std::size_t files, double budget) {
  using clock = std::chrono::steady_clock;
  auto gen = gq::walk(root);
  auto it = gen.begin();
  const auto start = clock::now();
  for (std::size_t i = 1; i < files; ++i) {
    ++it;
    if (i % 64 == 0 && std::chrono::duration<double>(clock::now() - start).count() > budget) return -1;
  }
  return std::chrono::duration<double>(clock::now() - start).count();
}

}  // namespace

TEST(walk_custo_por_item, "walk(): custo por arquivo não cresce com a profundidade do diretório") {
  constexpr int kDepth = 1500;
  constexpr std::size_t kFiles = 3000;
  gq::Dir deep("r");
  gq::Dir* current = &deep;
  for (int i = 0; i < kDepth; ++i) current = &current->add_dir("d");
  // Diretório raso com caminhos do mesmo tamanho: mesmo custo de montar strings.
  gq::Dir shallow(std::string(2 * kDepth + 1, 'r'));
  for (std::size_t i = 0; i < kFiles; ++i) {
    current->add_file("f" + std::to_string(i));
    shallow.add_file("f" + std::to_string(i));
  }
  double best_deep = 1e9;
  double best_shallow = 1e9;
  for (int round = 0; round < 3; ++round) {
    const double d = time_bottom_files(deep, kFiles, 4.0);
    if (d < 0) {
      ::minitest::fail(__LINE__, "listar 3000 arquivos a 1500 níveis de profundidade levou mais de 4 s — "
                                 "cada arquivo atravessa todos os níveis de corrotinas?");
      return;
    }
    best_deep = std::min(best_deep, d);
    best_shallow = std::min(best_shallow, time_bottom_files(shallow, kFiles, 4.0));
  }
  if (best_deep > 5 * best_shallow + 0.02) {
    ::minitest::fail(__LINE__, "a 1500 níveis cada arquivo custa " + std::to_string(best_deep / best_shallow) +
                                   "x mais que na raiz (" + std::to_string(best_deep) + " s vs " +
                                   std::to_string(best_shallow) + " s)");
  }
}

int main(int argc, char** argv) { return ::minitest::main(argc, argv); }
