// test_03_bordas.cpp — casos de borda da API.
#include <cstdint>
#include <limits>
#include <vector>

#include "check.h"
#include "sales_store.h"

namespace {

gq::Order order(std::uint64_t id, std::uint16_t store, std::uint16_t day, std::uint32_t qty,
                std::int64_t amount, gq::Status status) {
    gq::Order o;
    o.id = id;
    o.sku = 1;
    o.store = store;
    o.day = day;
    o.quantity = qty;
    o.amount_cents = amount;
    o.status = status;
    return o;
}

gq::Refund refund(std::uint16_t day, std::int64_t amount) {
    gq::Refund r;
    r.order_id = 1;
    r.day = day;
    r.amount_cents = amount;
    return r;
}

}  // namespace

TEST(intervalos_de_dias, "refunded_cents: intervalo invertido é vazio; extremos e limite de valor inclusivos") {
    gq::SalesStore s(1, 10);
    s.load_catalog({});
    s.add_refund(refund(0, 100));
    s.add_refund(refund(9, 200));
    s.add_refund(refund(5, 300));
    s.add_refund(refund(5, 50));
    CHECK(s.refunded_cents(5, 4, 0) == 0, "refunded_cents(5, 4, 0) = " << s.refunded_cents(5, 4, 0));
    CHECK(s.refunded_cents(9, 0, 0) == 0, "refunded_cents(9, 0, 0) = " << s.refunded_cents(9, 0, 0));
    CHECK(s.refunded_cents(0, 0, 0) == 100, "refunded_cents(0, 0, 0) = " << s.refunded_cents(0, 0, 0));
    CHECK(s.refunded_cents(9, 9, 0) == 200, "refunded_cents(9, 9, 0) = " << s.refunded_cents(9, 9, 0));
    CHECK(s.refunded_cents(0, 9, 0) == 650, "refunded_cents(0, 9, 0) = " << s.refunded_cents(0, 9, 0));
    CHECK(s.refunded_cents(0, 65535, 0) == 650, "refunded_cents(0, 65535, 0) = " << s.refunded_cents(0, 65535, 0));
    CHECK(s.refunded_cents(5, 5, 300) == 300, "refunded_cents(5, 5, 300): o limite é inclusivo, veio "
                                                  << s.refunded_cents(5, 5, 300));
    CHECK(s.refunded_cents(5, 5, 301) == 0, "refunded_cents(5, 5, 301) = " << s.refunded_cents(5, 5, 301));
    CHECK(s.refunded_cents(0, 9, -1) == 650, "refunded_cents(0, 9, -1) = " << s.refunded_cents(0, 9, -1));
}

TEST(mascara_de_status, "revenue: bits acima de 3 não selecionam nada; limite de valor inclusivo") {
    gq::SalesStore s(2, 2);
    s.load_catalog({});
    s.add_order(order(1, 0, 0, 1, 100, gq::Status::Pending));
    s.add_order(order(2, 1, 1, 1, 200, gq::Status::Paid));
    s.add_order(order(3, 1, 0, 1, 400, gq::Status::Shipped));
    s.add_order(order(4, 0, 1, 1, 800, gq::Status::Cancelled));
    CHECK(s.revenue(0xF0, 0) == 0, "revenue(0xF0, 0) = " << s.revenue(0xF0, 0));
    CHECK(s.revenue(0xFF, 0) == 1'500, "revenue(0xFF, 0) = " << s.revenue(0xFF, 0));
    CHECK(s.revenue(0b0001, 0) == 100, "revenue(pendentes) = " << s.revenue(0b0001, 0));
    CHECK(s.revenue(0b1111, 400) == 1'200, "revenue(tudo, >= 400) = " << s.revenue(0b1111, 400));
    CHECK(s.revenue(0b1111, 801) == 0, "revenue(tudo, >= 801) = " << s.revenue(0b1111, 801));
    CHECK(s.revenue(0b1111, std::numeric_limits<std::int64_t>::min()) == 1'500, "revenue(tudo, INT64_MIN)");
}

TEST(lojas_e_dias_nos_extremos, "units_by_day: primeira/última loja e dia, lista vazia e acúmulo no mesmo dia") {
    const std::uint16_t stores = 70, days = 400;
    gq::SalesStore s(stores, days);
    s.load_catalog({});
    std::uint64_t id = 1;
    for (int i = 0; i < 1000; ++i) s.add_order(order(id++, stores - 1, days - 1, 3, 1, gq::Status::Paid));
    s.add_order(order(id++, 0, 0, 7, 1, gq::Status::Paid));
    s.add_order(order(id++, 0, days - 1, 0, 1, gq::Status::Paid));
    const std::vector<std::uint16_t> none;
    CHECK(s.units_by_day(none) == std::vector<std::int64_t>(days, 0), "units_by_day({}) deveria ter só zeros");
    const std::vector<std::uint16_t> last = {stores - 1};
    std::vector<std::int64_t> want(days, 0);
    want[days - 1] = 3000;
    CHECK(s.units_by_day(last) == want, "units_by_day({última loja}): esperado 3000 no último dia");
    const std::vector<std::uint16_t> both = {stores - 1, 0};
    want[0] = 7;
    CHECK(s.units_by_day(both) == want, "units_by_day({última, primeira}): esperado 7 no dia 0 e 3000 no último");
}

TEST(catalogo_extremos, "basket_value e find_product com SKUs extremos, quantidades 0 e enormes") {
    gq::SalesStore s(1, 1);
    const std::uint32_t max_sku = std::numeric_limits<std::uint32_t>::max();
    const std::vector<gq::Product> cat = {
        {max_sku, 7, "ultimo", "x", "y"}, {0, 11, "zero", "x", "y"}, {500, 89'999, "caro", "x", "y"}};
    s.load_catalog(cat);
    const std::uint32_t big = std::numeric_limits<std::uint32_t>::max();
    const std::vector<gq::BasketLine> basket = {{0, 2}, {max_sku, 1}, {499, 1}, {501, 1},
                                                {500, 0}, {max_sku - 1, 5}, {500, big}};
    const gq::BasketTotal t = s.basket_value(basket);
    const std::int64_t expected = 22 + 7 + static_cast<std::int64_t>(big) * 89'999;
    CHECK(t.total_cents == expected, "basket_value total = " << t.total_cents << ", esperado " << expected);
    CHECK(t.unknown_lines == 3, "basket_value unknown_lines = " << t.unknown_lines << ", esperado 3");
    const std::vector<gq::BasketLine> empty;
    CHECK(s.basket_value(empty) == gq::BasketTotal{}, "basket_value({}) deveria ser {0, 0}");
    for (const gq::Product& p : cat) {
        const auto fp = s.find_product(p.sku);
        CHECK(fp.has_value() && *fp == p, "find_product(" << p.sku << ") não devolveu o produto original");
    }
    CHECK(!s.find_product(1).has_value(), "find_product(1) deveria ser nullopt");
}
