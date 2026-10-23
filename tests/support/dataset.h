// dataset.h — gerador determinístico de dados do painel e modelo de referência.
//
// Tudo deriva de uma semente (splitmix64): o mesmo (seed, formato) produz
// exatamente os mesmos pedidos, devoluções, catálogo e consultas em qualquer
// máquina. O Model responde às consultas da forma mais direta possível, a
// partir dos vetores originais: é o gabarito dos testes, não um exemplo de
// desempenho.
#ifndef GITQUEST_DATASET_H
#define GITQUEST_DATASET_H

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sales_store.h"

namespace gq::test {

class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // Inteiro em [lo, hi] (o viés do módulo é irrelevante aqui).
    std::uint64_t between(std::uint64_t lo, std::uint64_t hi) { return lo + next() % (hi - lo + 1); }
    bool chance(unsigned percent) { return next() % 100 < percent; }

private:
    std::uint64_t state_;
};

struct Shape {
    std::uint16_t stores = 1;
    std::uint16_t days = 1;
    std::size_t products = 0;
    std::size_t orders = 0;
    unsigned refund_percent = 25;  // chance de cada pedido ser seguido por uma devolução
};

// Um evento de ingestão, na ordem em que chega ao painel.
struct Event {
    bool is_refund = false;
    Order order;
    Refund refund;
};

struct Dataset {
    Shape shape;
    std::vector<Product> catalog;
    std::vector<Event> events;
};

inline std::string words(Rng& rng, std::size_t min_len, std::size_t max_len) {
    static const char* const kWords[] = {"rua",   "das",    "flores", "bloco",     "apto", "fundos",
                                         "portao", "azul",  "perto",  "padaria",   "entregar", "manha",
                                         "tarde", "interfone", "casa", "sala",     "norte", "sul"};
    const std::size_t target = rng.between(min_len, max_len);
    std::string s;
    while (s.size() < target) {
        if (!s.empty()) s += ' ';
        s += kWords[rng.next() % std::size(kWords)];
    }
    s.resize(target);
    return s;
}

inline Dataset make_dataset(const Shape& shape, std::uint64_t seed) {
    Rng rng(seed);
    Dataset d;
    d.shape = shape;

    std::unordered_set<std::uint32_t> seen;
    std::vector<std::uint32_t> skus;
    while (skus.size() < shape.products) {
        const auto sku = static_cast<std::uint32_t>(rng.between(10'000'000, 99'999'999));
        if (seen.insert(sku).second) skus.push_back(sku);
    }
    for (std::uint32_t sku : skus) {
        Product p;
        p.sku = sku;
        p.price_cents = static_cast<std::int64_t>(rng.between(49, 89'999));
        p.name = "Produto " + std::to_string(sku) + " " + words(rng, 4, 30);
        p.category = words(rng, 3, 14);
        p.supplier = "Fornecedor " + words(rng, 6, 24);
        d.catalog.push_back(std::move(p));
    }

    std::vector<std::uint64_t> ids;
    std::unordered_set<std::uint64_t> used_ids;
    static const char* const kCoupons[] = {"", "", "", "BOAVISTA10", "FRETEGRATIS", "CLIENTE-OURO-2024"};
    for (std::size_t i = 0; i < shape.orders; ++i) {
        Event e;
        Order& o = e.order;
        do {
            o.id = rng.next() >> 8;
        } while (!used_ids.insert(o.id).second);
        o.customer_id = static_cast<std::uint32_t>(rng.between(1, 500'000));
        const bool known = !d.catalog.empty() && rng.chance(95);
        o.sku = known ? d.catalog[rng.next() % d.catalog.size()].sku
                      : static_cast<std::uint32_t>(rng.between(1, 9'999'999));
        o.store = static_cast<std::uint16_t>(rng.between(0, shape.stores - 1u));
        o.day = static_cast<std::uint16_t>(rng.between(0, shape.days - 1u));
        o.quantity = static_cast<std::uint32_t>(rng.between(1, 12));
        o.amount_cents = static_cast<std::int64_t>(rng.between(100, 250'000));
        const std::uint64_t st = rng.next() % 100;
        o.status = st < 10 ? Status::Pending
                 : st < 60 ? Status::Paid
                 : st < 92 ? Status::Shipped
                           : Status::Cancelled;
        o.coupon = kCoupons[rng.next() % std::size(kCoupons)];
        o.shipping_address = words(rng, 20, 60);
        o.notes = words(rng, 0, 40);
        ids.push_back(o.id);
        d.events.push_back(std::move(e));

        if (rng.chance(shape.refund_percent)) {
            Event r;
            r.is_refund = true;
            r.refund.order_id = rng.chance(97) ? ids[rng.next() % ids.size()] : rng.next() >> 8;
            r.refund.day = static_cast<std::uint16_t>(rng.between(0, shape.days - 1u));
            r.refund.amount_cents = static_cast<std::int64_t>(rng.between(100, 120'000));
            r.refund.reason = static_cast<std::uint8_t>(rng.between(0, 7));
            r.refund.comment = words(rng, 10, 48);
            d.events.push_back(std::move(r));
        }
    }
    return d;
}

inline void ingest(SalesStore& store, const Dataset& d) {
    store.load_catalog(d.catalog);
    for (const Event& e : d.events) {
        if (e.is_refund) {
            store.add_refund(e.refund);
        } else {
            store.add_order(e.order);
        }
    }
}

// Cesta de compras: SKUs do catálogo (e ~10% desconhecidos), em ordem aleatória.
inline std::vector<BasketLine> make_basket(const Dataset& d, std::size_t lines, std::uint64_t seed) {
    Rng rng(seed);
    std::vector<BasketLine> basket;
    basket.reserve(lines);
    for (std::size_t i = 0; i < lines; ++i) {
        BasketLine l;
        const bool known = !d.catalog.empty() && rng.chance(90);
        l.sku = known ? d.catalog[rng.next() % d.catalog.size()].sku
                      : static_cast<std::uint32_t>(rng.between(1, 99'999'999));
        l.quantity = static_cast<std::uint32_t>(rng.between(0, 20));
        basket.push_back(l);
    }
    return basket;
}

// Subconjunto de lojas (sem repetição, em ordem crescente) — o filtro de região do painel.
inline std::vector<std::uint16_t> make_store_list(const Dataset& d, unsigned percent, std::uint64_t seed) {
    Rng rng(seed);
    std::vector<std::uint16_t> stores;
    for (std::uint32_t s = 0; s < d.shape.stores; ++s) {
        if (rng.chance(percent)) stores.push_back(static_cast<std::uint16_t>(s));
    }
    return stores;
}

// ---- modelo de referência (direto, sem nenhuma preocupação com desempenho) ----

class Model {
public:
    explicit Model(const Dataset& d) : d_(d) {
        for (const Product& p : d.catalog) price_[p.sku] = p.price_cents;
        for (const Event& e : d.events) {
            if (e.is_refund) {
                refunds_.push_back(&e.refund);
            } else {
                orders_[e.order.id] = &e.order;
            }
        }
    }

    std::int64_t revenue(std::uint8_t mask, std::int64_t min_amount) const {
        std::int64_t t = 0;
        for (const Event& e : d_.events) {
            if (e.is_refund) continue;
            const Order& o = e.order;
            if (((mask >> static_cast<unsigned>(o.status)) & 1u) != 0 && o.amount_cents >= min_amount) {
                t += o.amount_cents;
            }
        }
        return t;
    }

    std::vector<std::int64_t> units_by_day(std::span<const std::uint16_t> stores) const {
        std::vector<std::int64_t> per_store(d_.shape.stores, 0);
        std::vector<std::int64_t> totals(d_.shape.days, 0);
        // Quantas vezes cada loja aparece na lista (repetições contam de novo).
        std::vector<std::int64_t> weight(d_.shape.stores, 0);
        for (std::uint16_t s : stores) ++weight[s];
        for (const Event& e : d_.events) {
            if (!e.is_refund) {
                totals[e.order.day] += weight[e.order.store] * static_cast<std::int64_t>(e.order.quantity);
            }
        }
        return totals;
    }

    std::int64_t refunded_cents(std::uint16_t first, std::uint16_t last, std::int64_t min_amount) const {
        std::int64_t t = 0;
        for (const Refund* r : refunds_) {
            if (r->day >= first && r->day <= last && r->amount_cents >= min_amount) {
                t += r->amount_cents;
            }
        }
        return t;
    }

    BasketTotal basket_value(std::span<const BasketLine> lines) const {
        BasketTotal r;
        for (const BasketLine& l : lines) {
            auto it = price_.find(l.sku);
            if (it == price_.end()) {
                ++r.unknown_lines;
            } else {
                r.total_cents += static_cast<std::int64_t>(l.quantity) * it->second;
            }
        }
        return r;
    }

    const Order* order(std::uint64_t id) const {
        auto it = orders_.find(id);
        return it == orders_.end() ? nullptr : it->second;
    }
    std::size_t order_count() const { return orders_.size(); }
    std::size_t refund_count() const { return refunds_.size(); }

private:
    const Dataset& d_;
    std::unordered_map<std::uint32_t, std::int64_t> price_;
    std::unordered_map<std::uint64_t, const Order*> orders_;
    std::vector<const Refund*> refunds_;
};

}  // namespace gq::test

#endif
