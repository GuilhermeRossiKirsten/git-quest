// False sharing verificado de forma ESTRUTURAL e determinística.
//
// Medir false sharing com tempo, perf ou cachegrind num container com CPUs
// compartilhadas dá resultados que variam de uma execução para outra. A causa,
// porém, é estrutural: o que o produtor escreve e o que o consumidor escreve
// não podem morar no mesmo bloco de 64 bytes (linha de cache). Este teste:
//
//   1. mede os endereços REAIS dos dois índices (via ProbeIndex) em filas no
//      heap e na pilha e exige que fiquem em linhas diferentes;
//   2. confere que layout_info() descreve a fila de verdade (offsets reais);
//   3. usa layout_info() para checar também as cópias locais dos índices;
//   4. exige alignof >= 64, para que a fila comece numa linha própria e nada
//      vizinho (outra fila num array, outro objeto) compartilhe as linhas dela.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "support/probe_index.h"
#include "../src/market_feed.h"
#include "../src/queue_layout.h"
#include "../src/spsc_queue.h"

namespace {

std::size_t line_of(std::size_t offset) { return offset / kCacheLineSize; }

std::uintptr_t address_of(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

// Regras de layout que layout_info() precisa satisfazer (para qualquer T).
void check_reported_layout(const QueueLayout& info, std::size_t real_size, std::size_t real_align, const char* what) {
    CHECK(info.object_size == real_size && info.object_align == real_align,
          what << ": layout_info() informa sizeof=" << info.object_size << "/alignof=" << info.object_align
               << ", mas o real é " << real_size << "/" << real_align);
    CHECK(real_align >= kCacheLineSize,
          what << ": alignof(fila) == " << real_align << "; precisa ser >= " << kCacheLineSize
               << " para a fila começar numa linha de cache própria (senão os offsets não dizem em que linha cada campo "
                  "cai, e um objeto vizinho pode dividir a linha com um índice)");
    CHECK(info.producer_index != QueueLayout::kAbsent && info.consumer_index != QueueLayout::kAbsent,
          what << ": layout_info() precisa informar os dois índices");
    CHECK(info.producer_cache != QueueLayout::kAbsent && info.consumer_cache != QueueLayout::kAbsent,
          what << ": layout_info() informa que a fila não guarda cópias locais do índice oposto "
                  "(producer_cache/consumer_cache ausentes)");
    const std::size_t fields[] = {info.producer_index, info.consumer_index, info.producer_cache, info.consumer_cache};
    for (std::size_t f : fields) {
        CHECK(f + sizeof(std::size_t) <= real_size, what << ": layout_info() informa um offset (" << f
                                                         << ") fora do objeto (sizeof " << real_size << ")");
    }
    CHECK(std::set<std::size_t>(std::begin(fields), std::end(fields)).size() == 4,
          what << ": layout_info() informa dois campos no mesmo offset");
    const std::set<std::size_t> producer_lines = {line_of(info.producer_index), line_of(info.producer_cache)};
    const std::set<std::size_t> consumer_lines = {line_of(info.consumer_index), line_of(info.consumer_cache)};
    for (std::size_t line : producer_lines) {
        CHECK(consumer_lines.count(line) == 0,
              what << ": a linha de cache " << line << " (bytes " << line * kCacheLineSize << ".."
                   << (line + 1) * kCacheLineSize - 1
                   << ") tem campos escritos pelo produtor E pelo consumidor — false sharing (producer_index="
                   << info.producer_index << ", producer_cache=" << info.producer_cache
                   << ", consumer_index=" << info.consumer_index << ", consumer_cache=" << info.consumer_cache << ")");
    }
}

// Descobre os índices reais de uma fila instrumentada: um push e um pop em fases distintas.
void locate_indices(SpscQueue<int, ProbeIndex>& q, const probe::Entry*& producer, const probe::Entry*& consumer) {
    probe::current_phase = probe::kProducer;
    CHECK(q.try_push(1), "try_push falhou numa fila vazia");
    probe::current_phase = probe::kConsumer;
    std::optional<int> v = q.try_pop();
    CHECK(v && *v == 1, "try_pop deveria devolver o item 1");
    probe::current_phase = probe::kNone;
    producer = nullptr;
    consumer = nullptr;
    for (const probe::Entry* e : probe::inside(&q, sizeof(q))) {
        if (e->by_phase[probe::kProducer].stores > 0) {
            producer = e;
        } else if (e->by_phase[probe::kConsumer].stores > 0) {
            consumer = e;
        }
    }
    CHECK(producer != nullptr && consumer != nullptr,
          "não foi possível localizar os dois índices IndexT dentro da fila (o produtor escreve um, o consumidor o outro)");
}

void check_real_addresses(SpscQueue<int, ProbeIndex>& q, const char* where) {
    const probe::Entry* producer = nullptr;
    const probe::Entry* consumer = nullptr;
    locate_indices(q, producer, consumer);
    const std::uintptr_t p = address_of(producer->address);
    const std::uintptr_t c = address_of(consumer->address);
    CHECK(p / kCacheLineSize != c / kCacheLineSize,
          where << ": os índices do produtor e do consumidor estão na MESMA linha de cache de 64 bytes (estão a "
                << (p > c ? p - c : c - p)
                << " bytes um do outro): cada escrita de um núcleo invalida a linha que o outro está usando "
                   "(false sharing)");
    const QueueLayout info = q.layout_info();
    CHECK(info.producer_index == p - address_of(&q) && info.consumer_index == c - address_of(&q),
          where << ": layout_info() não confere com a memória real: informa producer_index=" << info.producer_index
                << " e consumer_index=" << info.consumer_index << ", mas os índices estão nos offsets "
                << p - address_of(&q) << " e " << c - address_of(&q));
    CHECK(address_of(&q) % kCacheLineSize == 0,
          where << ": a fila começa no endereço " << address_of(&q) << ", que não é múltiplo de " << kCacheLineSize);
}

}  // namespace

TEST(layout_indices_separados, "endereços reais: índices do produtor e do consumidor em linhas de cache diferentes (heap, pilha e array)") {
    auto heap = std::make_unique<SpscQueue<int, ProbeIndex>>(16);
    check_real_addresses(*heap, "fila no heap");
    SpscQueue<int, ProbeIndex> stack(16);
    check_real_addresses(stack, "fila na pilha");
    // Duas filas lado a lado: o fim de uma não pode dividir linha com o começo da outra.
    auto pair = std::make_unique<std::optional<SpscQueue<int, ProbeIndex>>[]>(2);
    pair[0].emplace(4);
    pair[1].emplace(4);
    check_real_addresses(*pair[0], "1ª fila de um array");
    check_real_addresses(*pair[1], "2ª fila de um array");
}

TEST(layout_info_linhas, "layout_info(): alinhamento >= 64 e campos do produtor e do consumidor (índices e cópias locais) em linhas separadas") {
    {
        SpscQueue<int, ProbeIndex> q(8);
        check_reported_layout(q.layout_info(), sizeof(q), alignof(SpscQueue<int, ProbeIndex>), "SpscQueue<int, ProbeIndex>");
    }
    {
        SpscQueue<int> q(8);
        check_reported_layout(q.layout_info(), sizeof(q), alignof(SpscQueue<int>), "SpscQueue<int>");
    }
    {
        SpscQueue<std::string> q(1000);
        check_reported_layout(q.layout_info(), sizeof(q), alignof(SpscQueue<std::string>), "SpscQueue<std::string>");
    }
    {
        SpscQueue<Quote> q(3);
        check_reported_layout(q.layout_info(), sizeof(q), alignof(SpscQueue<Quote>), "SpscQueue<Quote>");
    }
}
