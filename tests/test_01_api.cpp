// test_01_api.cpp — contrato da API pública com um cenário pequeno, feito à mão.
#include <cstdint>
#include <vector>

#include "check.h"
#include "sales_store.h"

namespace {

gq::Order order(std::uint64_t id, std::uint16_t store, std::uint16_t day, std::uint32_t qty,
                std::int64_t amount, gq::Status status, std::uint32_t sku = 10'000'001) {
    gq::Order o;
    o.id = id;
    o.customer_id = static_cast<std::uint32_t>(id * 10);
    o.sku = sku;
    o.store = store;
    o.day = day;
    o.quantity = qty;
    o.amount_cents = amount;
    o.status = status;
    o.coupon = id % 2 == 0 ? "BOAVISTA10" : "";
    o.shipping_address = "Rua das Flores, 123, bloco B, apartamento 42 - fundos";
    o.notes = "entregar pela manha, interfone quebrado";
    return o;
}

gq::Refund refund(std::uint64_t order_id, std::uint16_t day, std::int64_t amount) {
    gq::Refund r;
    r.order_id = order_id;
    r.day = day;
    r.amount_cents = amount;
    r.reason = 3;
    r.comment = "produto chegou amassado, cliente pediu estorno";
    return r;
}

std::vector<gq::Product> catalog() {
    return {
        {20'000'000, 1'990, "Cafe torrado 500 g extra forte", "mercearia", "Torrefacao Serra Azul Ltda"},
        {10'000'001, 450, "Pao frances", "padaria", "Padaria Boa Vista"},
        {30'000'003, 12'900, "Azeite extra virgem 500 ml importado", "mercearia", "Importadora Norte"},
    };
}

}  // namespace

TEST(store_vazio, "um painel vazio responde zero para todas as consultas") {
    gq::SalesStore s(4, 7);
    s.load_catalog({});
    CHECK(s.order_count() == 0, "order_count() = " << s.order_count());
    CHECK(s.refund_count() == 0, "refund_count() = " << s.refund_count());
    CHECK(s.revenue(0b1111, 0) == 0, "revenue() = " << s.revenue(0b1111, 0));
    const std::vector<std::uint16_t> all = {0, 1, 2, 3};
    const std::vector<std::int64_t> units = s.units_by_day(all);
    CHECK(units == std::vector<std::int64_t>(7, 0), "units_by_day() deveria ter 7 zeros");
    CHECK(s.refunded_cents(0, 6, 0) == 0, "refunded_cents() deveria ser 0");
    const std::vector<gq::BasketLine> basket = {{10'000'001, 3}};
    const gq::BasketTotal t = s.basket_value(basket);
    CHECK(t.total_cents == 0 && t.unknown_lines == 1,
          "cesta com catálogo vazio: total " << t.total_cents << ", desconhecidas " << t.unknown_lines);
    CHECK(!s.find_order(1).has_value(), "find_order() num painel vazio deveria ser nullopt");
    CHECK(!s.find_product(10'000'001).has_value(), "find_product() num catálogo vazio deveria ser nullopt");
}

TEST(cenario_pequeno, "cenário pequeno com respostas calculadas à mão") {
    gq::SalesStore s(3, 5);
    s.load_catalog(catalog());
    s.add_order(order(101, 0, 0, 2, 1'000, gq::Status::Paid));
    s.add_order(order(102, 2, 4, 5, 9'000, gq::Status::Shipped));
    s.add_refund(refund(101, 1, 400));
    s.add_order(order(103, 1, 4, 1, 50, gq::Status::Cancelled));
    s.add_order(order(104, 2, 0, 3, 2'500, gq::Status::Pending));
    s.add_refund(refund(999, 4, 7'000));  // pedido desconhecido: a devolução conta mesmo assim
    s.add_order(order(105, 0, 4, 4, 3'000, gq::Status::Paid));

    CHECK(s.order_count() == 5, "order_count() = " << s.order_count());
    CHECK(s.refund_count() == 2, "refund_count() = " << s.refund_count());

    CHECK(s.revenue(0b0010, 0) == 4'000, "revenue(pagos) = " << s.revenue(0b0010, 0) << ", esperado 4000");
    CHECK(s.revenue(0b0110, 0) == 13'000, "revenue(pagos+enviados) = " << s.revenue(0b0110, 0));
    CHECK(s.revenue(0b1111, 2'500) == 14'500, "revenue(tudo, >= 2500) = " << s.revenue(0b1111, 2'500));
    CHECK(s.revenue(0b1000, 0) == 50, "revenue(cancelados) = " << s.revenue(0b1000, 0));
    CHECK(s.revenue(0, 0) == 0, "revenue(mask 0) = " << s.revenue(0, 0));

    const std::vector<std::uint16_t> s02 = {0, 2};
    CHECK((s.units_by_day(s02) == std::vector<std::int64_t>{5, 0, 0, 0, 9}),
          "units_by_day({0, 2}) deveria ser {5, 0, 0, 0, 9}");
    const std::vector<std::uint16_t> s1 = {1};
    CHECK((s.units_by_day(s1) == std::vector<std::int64_t>{0, 0, 0, 0, 1}),
          "units_by_day({1}) deveria ser {0, 0, 0, 0, 1}");
    const std::vector<std::uint16_t> twice = {2, 2};
    CHECK((s.units_by_day(twice) == std::vector<std::int64_t>{6, 0, 0, 0, 10}),
          "units_by_day({2, 2}): loja repetida conta de novo, esperado {6, 0, 0, 0, 10}");

    CHECK(s.refunded_cents(0, 4, 0) == 7'400, "refunded_cents(0, 4, 0) = " << s.refunded_cents(0, 4, 0));
    CHECK(s.refunded_cents(1, 1, 0) == 400, "refunded_cents(1, 1, 0) = " << s.refunded_cents(1, 1, 0));
    CHECK(s.refunded_cents(0, 4, 500) == 7'000, "refunded_cents(0, 4, 500) = " << s.refunded_cents(0, 4, 500));
    CHECK(s.refunded_cents(2, 3, 0) == 0, "refunded_cents(2, 3, 0) = " << s.refunded_cents(2, 3, 0));

    const std::vector<gq::BasketLine> basket = {
        {30'000'003, 1}, {10'000'001, 10}, {55'555'555, 2}, {20'000'000, 0}, {10'000'001, 1}};
    const gq::BasketTotal t = s.basket_value(basket);
    CHECK(t.total_cents == 12'900 + 4'500 + 450, "basket_value total = " << t.total_cents << ", esperado 17850");
    CHECK(t.unknown_lines == 1, "basket_value unknown_lines = " << t.unknown_lines << ", esperado 1");
}

TEST(registros_completos, "find_order/find_product devolvem todos os campos, como foram inseridos") {
    gq::SalesStore s(3, 5);
    const std::vector<gq::Product> cat = catalog();
    s.load_catalog(cat);
    const gq::Order a = order(7'000'000'000ULL, 2, 3, 9, 123'456, gq::Status::Shipped, 30'000'003);
    const gq::Order b = order(42, 0, 0, 1, 99, gq::Status::Pending);
    s.add_order(a);
    s.add_refund(refund(42, 0, 99));
    s.add_order(b);
    const auto fa = s.find_order(a.id);
    CHECK(fa.has_value() && *fa == a, "find_order(" << a.id << ") não devolveu o pedido original");
    const auto fb = s.find_order(b.id);
    CHECK(fb.has_value() && *fb == b, "find_order(42) não devolveu o pedido original");
    CHECK(!s.find_order(43).has_value(), "find_order(43) deveria ser nullopt");
    for (const gq::Product& p : cat) {
        const auto fp = s.find_product(p.sku);
        CHECK(fp.has_value() && *fp == p, "find_product(" << p.sku << ") não devolveu o produto original");
    }
    CHECK(!s.find_product(10'000'002).has_value(), "find_product(10000002) deveria ser nullopt");
}
