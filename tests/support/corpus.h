// corpus.h — gerador determinístico de linhas de log e implementação de
// referência (ingênua: std::map + std::string, sem arena nem pmr).
//
// O gerador sabe, para cada linha, se ela é válida e qual o conteúdo esperado;
// o modelo de referência é construído a partir disso, sem passar pelo parser
// do código em src/.
#ifndef GITQUEST_CORPUS_H
#define GITQUEST_CORPUS_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "../../src/log_line.h"

namespace gq {

// xorshift64*: mesma sequência em qualquer plataforma.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : s_(seed * 0x9E3779B97F4A7C15ULL + 1) {}
    std::uint64_t next() {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 0x2545F4914F6CDD1DULL;
    }
    std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }

private:
    std::uint64_t s_;
};

// Nomes curtos (cabem no SSO de std::string) e longos (não cabem).
inline const std::vector<std::string>& service_names() {
    static const std::vector<std::string> names = {
        "checkout-api",
        "inventory-reservation-service",
        "payment-gateway-adapter",
        "auth",
        "search-indexer-worker",
        "notification-dispatcher",
        "pricing-engine",
        "user-profile-service",
        "cart",
        "shipping-quote-calculator",
        "fraud-detection-pipeline",
        "recommendation-ranker",
    };
    return names;
}

struct Record {
    bool valid = false;
    std::string service;
    Level level = Level::Info;
    std::string message;
};

struct Corpus {
    std::vector<std::string> lines;
    std::vector<Record> records;
    std::size_t bytes = 0;  // soma dos tamanhos das linhas
};

inline std::string make_message(Rng& rng) {
    static const char* const words[] = {
        "request", "upstream", "timed", "out", "after", "retries", "cache", "miss", "for", "key",
        "connection", "reset", "by", "peer", "order", "reserved", "payment", "declined", "token",
        "expired", "slow", "query", "took", "ms", "queue", "depth", "exceeded", "threshold", "user",
        "session", "rebalanced", "partition", "lag", "circuit", "breaker", "opened", "half-open",
    };
    const std::size_t kind = rng.below(10);
    if (kind == 0) {
        return std::string();  // msg="" também é válida
    }
    std::string m;
    const std::size_t target = kind == 1 ? 4 + rng.below(8) : 24 + rng.below(110);
    while (m.size() < target) {
        if (!m.empty()) {
            m += ' ';
        }
        if (rng.below(5) == 0) {
            m += std::to_string(rng.below(100000));
        } else {
            m += words[rng.below(sizeof(words) / sizeof(words[0]))];
        }
    }
    return m;
}

// Linha malformada de um dos tipos que o parser precisa rejeitar.
inline std::string make_invalid(Rng& rng, const std::string& ts) {
    switch (rng.below(7)) {
    case 0: return ts + " level=CRIT svc=cart msg=\"disk full\"";
    case 1: return ts + " level=ERROR svc=checkout-api";
    case 2: return ts + " level=WARN svc=Checkout msg=\"upper case service\"";
    case 3: return ts + " level=INFO svc=auth msg=\"unterminated";
    case 4: return ts + " level=INFO svc=auth msg=\"two \" quotes\"";
    case 5: return std::string();
    default: return ts + "  level=INFO svc=auth msg=\"double space\"";
    }
}

// `n` linhas a partir da semente `seed`; ~6% malformadas.
inline Corpus make_corpus(std::size_t n, std::uint64_t seed) {
    static const Level levels[] = {Level::Debug, Level::Info, Level::Info, Level::Info, Level::Warn,
                                   Level::Warn,  Level::Error, Level::Error, Level::Fatal};
    Rng rng(seed);
    Corpus c;
    c.lines.reserve(n);
    c.records.reserve(n);
    const auto& names = service_names();
    for (std::size_t i = 0; i < n; ++i) {
        char ts[40];
        std::snprintf(ts, sizeof ts, "2026-10-22T%02zu:%02zu:%02zu.%03zuZ", (i / 3600000) % 24, (i / 60000) % 60,
                      (i / 1000) % 60, i % 1000);
        Record r;
        std::string line;
        if (rng.below(100) < 6) {
            line = make_invalid(rng, ts);
        } else {
            r.valid = true;
            // Distribuição desigual: alguns serviços aparecem bem mais.
            const std::size_t a = rng.below(names.size());
            const std::size_t b = rng.below(names.size());
            r.service = names[std::min(a, b)];
            r.level = levels[rng.below(sizeof(levels) / sizeof(levels[0]))];
            r.message = make_message(rng);
            line = std::string(ts) + " level=" + std::string(level_name(r.level)) + " svc=" + r.service +
                   " msg=\"" + r.message + "\"";
        }
        c.bytes += line.size();
        c.lines.push_back(std::move(line));
        c.records.push_back(std::move(r));
    }
    return c;
}

// Implementação de referência: o que o índice precisa responder.
struct Reference {
    std::vector<const Record*> accepted;
    std::size_t rejected = 0;
    std::map<std::string, std::array<std::uint64_t, kLevelCount>> per_level;

    void add(const Record& r) {
        if (!r.valid) {
            ++rejected;
            return;
        }
        accepted.push_back(&r);
        auto& counts = per_level[r.service];  // value-initialized: zeros
        ++counts[level_index(r.level)];
    }

    void add_all(const Corpus& c, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1)) {
        to = std::min(to, c.records.size());
        for (std::size_t i = from; i < to; ++i) {
            add(c.records[i]);
        }
    }

    std::uint64_t total(const std::string& service) const {
        auto it = per_level.find(service);
        if (it == per_level.end()) {
            return 0;
        }
        std::uint64_t t = 0;
        for (std::uint64_t v : it->second) {
            t += v;
        }
        return t;
    }

    std::vector<std::string> top(std::size_t k) const {
        std::vector<std::tuple<std::uint64_t, std::uint64_t, std::string>> rank;
        for (const auto& [name, counts] : per_level) {
            rank.emplace_back(counts[level_index(Level::Error)] + counts[level_index(Level::Fatal)], total(name),
                              name);
        }
        std::sort(rank.begin(), rank.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b)) {
                return std::get<0>(a) > std::get<0>(b);
            }
            if (std::get<1>(a) != std::get<1>(b)) {
                return std::get<1>(a) > std::get<1>(b);
            }
            return std::get<2>(a) < std::get<2>(b);
        });
        std::vector<std::string> out;
        for (std::size_t i = 0; i < rank.size() && i < k; ++i) {
            out.push_back(std::get<2>(rank[i]));
        }
        return out;
    }
};

}  // namespace gq

#endif
