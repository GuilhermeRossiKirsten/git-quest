// log_index.h — índice em memória de um lote de linhas de log.
//
// Cada LogIndex tem a sua própria Arena: as mensagens, os nomes de serviço, os
// nós do mapa e os contadores vivem nela e voltam ao `upstream` de uma vez
// quando o índice é destruído. É isso que deixa o ingest barato: milhões de
// alocações pequenas viram alguns poucos blocos grandes.
//
// Contrato (é o que os testes verificam):
//   * ingest() e absorb() não fazem NENHUMA alocação no heap global
//     (operator new): tudo vai para a arena do índice;
//   * depois de absorb(shard), o shard pode ser destruído: nada do índice
//     aponta para a memória dele;
//   * o resultado de top_services(k, out) — o vetor e cada string — é alocado
//     em `out` e continua válido depois de novas chamadas e mesmo depois que o
//     índice for destruído;
//   * os resultados das consultas são os mesmos de uma implementação ingênua
//     com std::map e std::string.
#ifndef GITQUEST_LOG_INDEX_H
#define GITQUEST_LOG_INDEX_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "arena.h"
#include "arena_resource.h"
#include "log_line.h"

class LogIndex {
public:
    explicit LogIndex(std::pmr::memory_resource* upstream = std::pmr::get_default_resource());

    LogIndex(const LogIndex&) = delete;
    LogIndex& operator=(const LogIndex&) = delete;

    // Indexa uma linha (formato em log_line.h). Linhas malformadas são
    // contadas em rejected() e ignoradas; nesse caso devolve false.
    bool ingest(std::string_view line);

    // Acrescenta as linhas do shard, na ordem dele, depois das já existentes.
    // O shard fica vazio e pode ser destruído logo em seguida.
    void absorb(LogIndex&& shard);

    std::size_t lines() const noexcept { return lines_.size(); }
    std::size_t rejected() const noexcept { return rejected_; }
    std::size_t services() const noexcept { return by_id_.size(); }

    // Dados da i-ésima linha aceita (0 <= i < lines()).
    std::string_view message(std::size_t i) const;
    std::string_view service(std::size_t i) const;
    Level level(std::size_t i) const;

    // Quantidade de linhas do serviço (todas, ou só as de um nível); 0 se o
    // serviço nunca apareceu.
    std::uint64_t count(std::string_view service) const;
    std::uint64_t count(std::string_view service, Level level) const;

    // Os k serviços com mais linhas ERROR + FATAL; empate: mais linhas no
    // total; depois, nome em ordem alfabética. O vetor e as strings são
    // alocados em `out`.
    std::pmr::vector<std::pmr::string> top_services(std::size_t k, std::pmr::memory_resource* out);

private:
    // Contadores de um serviço, um por linha de cache (64 bytes): os shards são
    // contados em threads diferentes e não devem disputar a mesma linha.
    struct alignas(64) ServiceStats {
        std::uint64_t per_level[kLevelCount] = {};
        std::uint64_t total = 0;
        std::uint32_t id = 0;
        std::string_view name;  // aponta para a chave em services_
    };

    struct LineInfo {
        std::uint32_t service;
        Level level;
    };

    ServiceStats& stats_for(std::string_view service);
    const ServiceStats* find_stats(std::string_view service) const;

    Arena arena_;
    ArenaResource resource_;
    std::pmr::monotonic_buffer_resource scratch_;  // rascunho de top_services()

    std::pmr::vector<std::string> messages_;
    std::pmr::vector<LineInfo> lines_;
    std::pmr::map<std::string, ServiceStats*, std::less<>> services_;
    std::pmr::vector<ServiceStats*> by_id_;
    std::size_t rejected_ = 0;
};

#endif
