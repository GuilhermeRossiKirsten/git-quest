// FORNECIDO PELO DESAFIO — NÃO edite. Relatório de demonstração do logscan:
// mostra os sintomas que o time de plataforma reclamou. Rode build/logscan.
#include <cstdio>
#include <exception>
#include <string>

#include "gq/logscan.hpp"

namespace {

// Log sintético de ~8 MiB: um ERROR a cada 7 linhas.
std::string synthetic_log(std::size_t lines_count) {
  static const char* const services[] = {"auth", "payments", "catalog", "checkout-gateway-eu"};
  std::string text = "# export do cluster sa-east-1\n";
  for (std::size_t i = 0; i < lines_count; ++i) {
    const char* level = i % 7 == 3 ? "ERROR" : "INFO";
    text += "2026-10-20T08:" + std::to_string(10 + i % 50) + ":00Z " + level + " " +
            services[i % 4] + " " + std::to_string(20 + i % 300) +
            " requisição #" + std::to_string(i) + " processada pelo pod checkout-7f9c\n";
  }
  return text;
}

double mib(std::size_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

}  // namespace

int main() {
  std::puts("== 1. Os 5 primeiros erros do log de ontem");
  gq::MemorySource log(synthetic_log(90'000));
  for (const gq::Record& rec : gq::first_matching(log, "ERROR", 5)) {
    std::printf("   %.*s %.*s %dms %.*s\n", static_cast<int>(rec.timestamp.size()), rec.timestamp.data(),
                static_cast<int>(rec.service.size()), rec.service.data(), rec.latency_ms,
                static_cast<int>(rec.message.size()), rec.message.data());
  }
  std::printf("   bytes lidos da fonte: %.2f MiB de %.2f MiB (aberturas=%zu, fechamentos=%zu)\n",
              mib(log.stats().bytes_read), mib(log.size()), log.stats().opens, log.stats().closes);

  std::puts("\n== 2. Disco de rede falhando no meio do arquivo");
  gq::MemorySource flaky(synthetic_log(1'000));
  flaky.fail_at(flaky.size() / 2, "EIO: conexão com o storage perdida");
  std::size_t count = 0;
  try {
    for (const gq::Record& rec : gq::records(flaky)) {
      static_cast<void>(rec);
      ++count;
    }
    std::printf("   relatório concluído com %zu registros (de 1000) e nenhum erro reportado\n", count);
  } catch (const std::exception& e) {
    std::printf("   erro entregue ao consumidor após %zu registros: %s\n", count, e.what());
  }

  std::puts("\n== 3. Inventário do bucket (árvore virtual)");
  gq::Dir bucket("bucket");
  gq::Dir* current = &bucket;
  for (int depth = 0; depth < 300; ++depth) {
    current->add_file("app-" + std::to_string(depth) + ".log");
    current = &current->add_dir("d" + std::to_string(depth));
  }
  gq::Dir::reset_copies();
  std::size_t files = 0;
  for (const std::string& path : gq::walk(bucket)) {
    if (files++ < 2) std::printf("   %s\n", path.c_str());
  }
  std::printf("   %zu arquivos listados; diretórios copiados durante a listagem: %zu\n", files,
              gq::Dir::copies());
  return 0;
}
