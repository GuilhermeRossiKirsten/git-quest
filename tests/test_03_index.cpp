// Testes do LogIndex: equivalência com a referência, alocações no heap global,
// blocos do upstream, absorb de shards e top_services.
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <vector>

#include "check.h"
#include "support/compare.h"
#include "support/corpus.h"
#include "support/heap_meter.h"
#include "support/resources.h"
#include "../src/log_index.h"

namespace {

void ingest_range(LogIndex& idx, const gq::Corpus& c, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        const bool accepted = idx.ingest(c.lines[i]);
        CHECK(accepted == c.records[i].valid, "ingest() da linha " << i << " devolveu " << accepted << ": \""
                                                                     << c.lines[i] << "\"");
    }
}

std::string join(const std::vector<std::string>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        s += (i ? ", " : "") + v[i];
    }
    return s + "]";
}

std::vector<std::string> to_std(const std::pmr::vector<std::pmr::string>& v) {
    std::vector<std::string> out;
    for (const auto& s : v) {
        out.emplace_back(s);
    }
    return out;
}

}  // namespace

TEST(index_equivalente_referencia, "o índice responde igual à implementação de referência (8 mil linhas, ~6% malformadas)") {
    const gq::Corpus corpus = gq::make_corpus(8000, 26);
    gq::Reference ref;
    ref.add_all(corpus);
    gq::MallocResource up;
    LogIndex idx(&up);
    ingest_range(idx, corpus, 0, corpus.lines.size());
    const std::string diff = gq::diff_with_reference(idx, ref);
    CHECK(diff.empty(), "divergência: " << diff);
}

TEST(index_ingest_sem_heap_global, "ingest() de 20 mil linhas faz 0 alocações no heap global (tudo na arena)") {
    const gq::Corpus corpus = gq::make_corpus(20000, 7);
    gq::MallocResource up;
    LogIndex idx(&up);
    long allocations = 0;
    std::size_t bytes = 0;
    {
        gq::HeapMeter meter;
        for (const std::string& line : corpus.lines) {
            idx.ingest(line);
        }
        allocations = meter.allocations();
        bytes = meter.bytes();
    }
    CHECK(allocations == 0, "ingest() de " << corpus.lines.size() << " linhas fez " << allocations
                                           << " alocação(ões) no heap global (" << bytes
                                           << " bytes) — algo escapou da arena (esperado: 0)");
    gq::Reference ref;
    ref.add_all(corpus);
    const std::string diff = gq::diff_with_reference(idx, ref);
    CHECK(diff.empty(), "divergência: " << diff);
}

TEST(index_ingest_em_blocos, "ingest() pede memória ao upstream em poucos blocos grandes") {
    const gq::Corpus corpus = gq::make_corpus(20000, 11);
    gq::MallocResource up;
    {
        LogIndex idx(&up);
        for (const std::string& line : corpus.lines) {
            idx.ingest(line);
        }
        CHECK(up.allocations() <= 200, "o índice chamou o upstream " << up.allocations() << " vezes para "
                                                                      << corpus.lines.size()
                                                                      << " linhas (orçamento: 200 blocos)");
        CHECK(up.bytes() <= corpus.bytes * 8, "o índice pediu " << up.bytes() << " bytes ao upstream para "
                                                                 << corpus.bytes << " bytes de log (limite: 8x)");
    }
    CHECK(up.outstanding() == 0, "destruir o índice deixou " << up.outstanding() << " bloco(s) sem devolver ao upstream");
}

TEST(index_absorb_shards, "absorb() de shards com arenas próprias: shards destruídos, índice íntegro, 0 alocações globais") {
    const gq::Corpus corpus = gq::make_corpus(9000, 99);
    gq::Reference ref;
    ref.add_all(corpus);
    gq::MallocResource up;
    LogIndex main_idx(&up);
    long allocations = 0;
    // Três shards, cada um na sua arena, absorvidos e destruídos em seguida.
    // O primeiro absorb encontra o índice principal vazio.
    for (std::size_t part = 0; part < 3; ++part) {
        auto shard = std::make_unique<LogIndex>(&up);
        ingest_range(*shard, corpus, part * 3000, (part + 1) * 3000);
        {
            gq::HeapMeter meter;
            main_idx.absorb(std::move(*shard));
            allocations += meter.allocations();
        }
        CHECK(shard->lines() == 0 && shard->services() == 0, "depois de absorb() o shard deveria ficar vazio");
        shard.reset();  // a arena do shard devolve os blocos ao upstream
        if (part == 0) {
            // Ler logo depois de destruir o primeiro shard (caminho "índice vazio").
            gq::Reference first;
            first.add_all(corpus, 0, 3000);
            const std::string diff = gq::diff_with_reference(main_idx, first);
            CHECK(diff.empty(), "depois do 1º absorb e da destruição do shard: " << diff);
        }
    }
    CHECK(allocations == 0, "absorb() fez " << allocations << " alocação(ões) no heap global (esperado: 0)");
    const std::string diff = gq::diff_with_reference(main_idx, ref);
    CHECK(diff.empty(), "depois de absorver os 3 shards: " << diff);
    CHECK(main_idx.ingest("2026-10-22T23:59:59Z level=FATAL svc=cart msg=\"" + std::string(200, 'z') + "\""),
          "o índice deveria continuar aceitando linhas depois de absorb()");
}

TEST(index_top_services, "top_services(k, out) acerta o ranking e aloca o vetor e as strings em out") {
    const gq::Corpus corpus = gq::make_corpus(6000, 2026);
    gq::Reference ref;
    ref.add_all(corpus);
    gq::MallocResource up, out;
    LogIndex idx(&up);
    ingest_range(idx, corpus, 0, corpus.lines.size());
    for (std::size_t k : {std::size_t{0}, std::size_t{1}, std::size_t{5}, std::size_t{12}, std::size_t{50}}) {
        long allocations = 0;
        // Construída a partir do retorno (movimento: o allocator vem junto).
        const std::pmr::vector<std::pmr::string> top = [&] {
            gq::HeapMeter meter;
            auto result = idx.top_services(k, &out);
            allocations = meter.allocations();
            return result;
        }();
        CHECK(allocations == 0, "top_services(" << k << ") fez " << allocations
                                                << " alocação(ões) no heap global (esperado: 0)");
        CHECK(to_std(top) == ref.top(k), "top_services(" << k << ") == " << join(to_std(top)) << ", esperado "
                                                         << join(ref.top(k)));
        if (k == 0) {
            continue;
        }
        CHECK(top.get_allocator().resource() == &out,
              "o vetor devolvido por top_services(" << k << ") não usa o memory_resource `out` passado pelo chamador");
        for (const auto& s : top) {
            CHECK(s.get_allocator().resource() == &out, "a string \"" << s << "\" não foi alocada em `out`");
        }
    }
    CHECK(out.allocations() > 0, "nada foi alocado em `out`");
}

TEST(index_top_services_sobrevive, "o resultado de top_services sobrevive a novas consultas e à destruição do índice") {
    const gq::Corpus corpus = gq::make_corpus(4000, 314);
    gq::Reference ref;
    ref.add_all(corpus);
    const std::vector<std::string> expected = ref.top(4);
    gq::MallocResource up, out;
    std::optional<std::pmr::vector<std::pmr::string>> first;
    {
        auto idx = std::make_unique<LogIndex>(&up);
        ingest_range(*idx, corpus, 0, corpus.lines.size());
        first.emplace(idx->top_services(4, &out));  // construída por movimento
        for (int i = 0; i < 3; ++i) {
            auto again = idx->top_services(12, &out);  // novas consultas reaproveitam o rascunho
            CHECK(again.size() == 12, "top_services(12) devolveu " << again.size() << " nomes");
        }
        CHECK(to_std(*first) == expected, "o primeiro resultado mudou depois de novas consultas: "
                                             << join(to_std(*first)) << ", esperado " << join(expected));
    }  // o índice (e tudo o que é dele) morre aqui
    CHECK(to_std(*first) == expected, "o resultado mudou depois que o índice foi destruído: " << join(to_std(*first)));
    CHECK(up.outstanding() == 0, "o índice destruído deixou " << up.outstanding() << " bloco(s) com o upstream");
}
