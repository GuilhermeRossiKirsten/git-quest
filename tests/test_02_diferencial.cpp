// test_02_diferencial.cpp — mesmas respostas que o gabarito em vários tamanhos e sementes.
//
// Para cada (formato, semente) o teste gera uma rede, alimenta o SalesStore e
// faz dezenas de consultas aleatórias, comparando cada resposta com o Model
// (tests/support/dataset.h). Também imprime um checksum de todas as respostas:
// ele precisa ser idêntico ao do gabarito.
#include <cstdint>
#include <string>
#include <vector>

#include "check.h"
#include "sales_store.h"
#include "support/dataset.h"

namespace {

using gq::test::Dataset;
using gq::test::Model;
using gq::test::Rng;
using gq::test::Shape;

struct Checksum {
    std::uint64_t h = 1469598103934665603ULL;
    void add(std::int64_t v) {
        h ^= static_cast<std::uint64_t>(v);
        h *= 1099511628211ULL;
    }
};

void run_case(const Shape& shape, std::uint64_t seed) {
    const Dataset d = gq::test::make_dataset(shape, seed);
    const Model model(d);
    gq::SalesStore store(shape.stores, shape.days);
    gq::test::ingest(store, d);

    const std::string where = "[lojas " + std::to_string(shape.stores) + ", dias " + std::to_string(shape.days) +
                              ", pedidos " + std::to_string(shape.orders) + ", semente " + std::to_string(seed) +
                              "] ";
    CHECK(store.order_count() == model.order_count(), where << "order_count() = " << store.order_count()
                                                            << ", esperado " << model.order_count());
    CHECK(store.refund_count() == model.refund_count(), where << "refund_count() = " << store.refund_count()
                                                              << ", esperado " << model.refund_count());

    Checksum got, want;
    Rng rng(seed * 7919 + 1);
    for (int q = 0; q < 24; ++q) {
        const auto mask = static_cast<std::uint8_t>(rng.between(0, 15));
        const auto min_amount = static_cast<std::int64_t>(rng.chance(30) ? 0 : rng.between(0, 260'000));
        const std::int64_t r = store.revenue(mask, min_amount);
        const std::int64_t m = model.revenue(mask, min_amount);
        CHECK(r == m, where << "revenue(" << int(mask) << ", " << min_amount << ") = " << r << ", esperado " << m);
        got.add(r);
        want.add(m);

        const auto a = static_cast<std::uint16_t>(rng.between(0, shape.days - 1u));
        const auto b = static_cast<std::uint16_t>(rng.between(0, shape.days - 1u));
        const auto min_refund = static_cast<std::int64_t>(rng.chance(50) ? 0 : rng.between(0, 130'000));
        const std::int64_t rr = store.refunded_cents(a, b, min_refund);
        const std::int64_t mr = model.refunded_cents(a, b, min_refund);
        CHECK(rr == mr, where << "refunded_cents(" << a << ", " << b << ", " << min_refund << ") = " << rr
                              << ", esperado " << mr);
        got.add(rr);
        want.add(mr);
    }
    for (unsigned pct : {0u, 3u, 40u, 100u}) {
        const std::vector<std::uint16_t> stores = gq::test::make_store_list(d, pct, seed + pct);
        const std::vector<std::int64_t> u = store.units_by_day(stores);
        const std::vector<std::int64_t> mu = model.units_by_day(stores);
        CHECK(u.size() == mu.size(), where << "units_by_day() devolveu " << u.size() << " posições, esperado "
                                           << mu.size());
        for (std::size_t day = 0; day < mu.size(); ++day) {
            CHECK(u[day] == mu[day], where << "units_by_day(" << stores.size() << " lojas)[" << day << "] = "
                                           << u[day] << ", esperado " << mu[day]);
            got.add(u[day]);
            want.add(mu[day]);
        }
    }
    for (std::size_t lines : {std::size_t{0}, std::size_t{1}, std::size_t{257}, std::size_t{5'000}}) {
        const std::vector<gq::BasketLine> basket = gq::test::make_basket(d, lines, seed + lines);
        const gq::BasketTotal t = store.basket_value(basket);
        const gq::BasketTotal mt = model.basket_value(basket);
        CHECK(t == mt, where << "basket_value(" << lines << " linhas) = {" << t.total_cents << ", "
                             << t.unknown_lines << "}, esperado {" << mt.total_cents << ", " << mt.unknown_lines
                             << "}");
        got.add(t.total_cents);
        got.add(t.unknown_lines);
        want.add(mt.total_cents);
        want.add(mt.unknown_lines);
    }
    // Uma amostra de registros completos (campos frios incluídos).
    for (std::size_t i = 0; i < d.events.size(); i += 1 + d.events.size() / 50) {
        const gq::test::Event& e = d.events[i];
        if (e.is_refund) continue;
        const auto o = store.find_order(e.order.id);
        CHECK(o.has_value() && *o == e.order, where << "find_order(" << e.order.id
                                                    << ") não devolveu o pedido original");
    }
    for (std::size_t i = 0; i < d.catalog.size(); i += 1 + d.catalog.size() / 50) {
        const auto p = store.find_product(d.catalog[i].sku);
        CHECK(p.has_value() && *p == d.catalog[i], where << "find_product(" << d.catalog[i].sku
                                                         << ") não devolveu o produto original");
    }
    CHECK(got.h == want.h, where << "checksum das respostas " << got.h << " != gabarito " << want.h);
}

}  // namespace

TEST(minusculo, "redes minúsculas (1 loja, 1 dia, até 3 pedidos) batem com o gabarito") {
    for (std::size_t orders : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
        Shape s;
        s.stores = 1;
        s.days = 1;
        s.products = orders;
        s.orders = orders;
        s.refund_percent = 60;
        for (std::uint64_t seed : {1u, 2u, 3u}) run_case(s, seed);
    }
}

TEST(pequeno, "redes pequenas (várias sementes) batem com o gabarito") {
    Shape s;
    s.stores = 17;
    s.days = 31;
    s.products = 120;
    s.orders = 900;
    for (std::uint64_t seed : {11u, 12u, 13u, 14u, 15u}) run_case(s, seed);
}

TEST(medio, "redes médias (várias sementes) batem com o gabarito") {
    Shape s;
    s.stores = 300;
    s.days = 365;
    s.products = 2'500;
    s.orders = 20'000;
    for (std::uint64_t seed : {101u, 202u, 303u}) run_case(s, seed);
}

TEST(formato_bench, "o formato da medição (1 500 lojas × 365 dias, 4 000 produtos) bate com o gabarito") {
    Shape s;
    s.stores = 1500;
    s.days = 365;
    s.products = 4000;
    s.orders = 30'000;
    s.refund_percent = 40;
    run_case(s, 0xBEEF);
}
