// Relatório diário do serviço "checkout", no formato chave=valor que o
// coletor de métricas importa. Usa apenas a biblioteca gq.
#include <iostream>
#include <string>
#include <vector>

#include "gq/metrics.hpp"

int main() {
  // Latência (ms) das últimas requisições.
  const std::vector<int> latency_ms{2, 3, 2, 3};
  // Resultado dos health checks: true = instância saudável.
  const std::vector<bool> healthy{true, true, false, true};
  // Endpoint de cada requisição, extraído do log de acesso.
  const std::vector<std::string> requests{"/login", "/cart", "/login", "/login"};
  // Uso de CPU por núcleo; o sensor às vezes reporta fora de [0, 1].
  std::vector<double> cpu{0.42, 1.3, -0.1};

  gq::Registry<std::string> config;
  config.add("region", "sa-east-1");
  config.add("version", "2.4.1");

  std::cout << "latency_mean_ms=" << gq::serialize(gq::mean(latency_ms)) << '\n';
  std::cout << "availability_pct=" << gq::serialize(100 * gq::mean(healthy)) << '\n';

  gq::clamp_all(cpu, 0, 1);
  std::cout << "cpu_mean=" << gq::serialize(gq::mean(cpu)) << '\n';

  gq::Histogram<const char*> hits;
  for (const auto& endpoint : requests) {
    hits.add(endpoint.c_str());
  }
  std::cout << "endpoints_distinct=" << gq::serialize(hits.distinct()) << '\n';
  std::cout << "hits_login=" << gq::serialize(hits.count(requests.front().c_str())) << '\n';

  // O exportador e a auditoria recebem, cada um, sua cópia da configuração.
  for (const auto& [key, value] : config.snapshot()) {
    std::cout << "config." << key << '=' << gq::serialize(value) << '\n';
  }
  for (const auto& [key, value] : config.snapshot()) {
    std::cout << "audit." << key << '=' << gq::serialize(value) << '\n';
  }
  std::cout << "config_has_region=" << gq::serialize(config.find("region") != nullptr) << '\n';
  return 0;
}
