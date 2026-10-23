// bench.cpp — carga do painel medida pelo simulador de cache do Valgrind.
//
// Monta uma rede grande (1 500 lojas, 365 dias, 120 000 pedidos, ~30 000
// devoluções, 4 000 produtos) e executa cada fase em uma região própria:
//
//   ingest          load_catalog + add_order/add_refund de todos os eventos
//   revenue         3 chamadas com filtros diferentes
//   units_by_day    2 chamadas com conjuntos de lojas diferentes
//   refunded_cents  3 chamadas com intervalos diferentes
//   basket_value    2 cestas de 100 000 linhas
//
// Sob `valgrind --tool=callgrind --collect-atstart=no`, só o que acontece entre
// os CALLGRIND_TOGGLE_COLLECT é contado, e cada região vira um arquivo
// (CALLGRIND_DUMP_STATS_AT). Antes de cada região a cache simulada é "esfriada"
// com uma varredura de 8 MiB, para que cada fase comece do mesmo estado não
// importa o que veio antes. Fora do Valgrind as macros não fazem nada.
//
// Todos os resultados são conferidos com o gabarito (tests/support/dataset.h):
// uma consulta rápida mas errada reprova aqui também.
#include <valgrind/callgrind.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "sales_store.h"
#include "support/dataset.h"

namespace {

using gq::test::Dataset;

volatile std::uint64_t g_sink = 0;

// Expulsa da cache simulada tudo o que a fase anterior deixou lá.
void chill_caches() {
    static std::vector<std::uint64_t> junk(1u << 20, 1);  // 8 MiB
    std::uint64_t acc = 0;
    for (std::size_t i = 0; i < junk.size(); i += 8) {
        junk[i] += acc;
        acc += junk[i];
    }
    g_sink = g_sink + acc;
}

int g_failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("DIVERGENCIA: %s\n", what.c_str());
        ++g_failures;
    }
}

}  // namespace

#define REGION(name, ...)                     \
    do {                                      \
        chill_caches();                       \
        CALLGRIND_ZERO_STATS;                 \
        CALLGRIND_TOGGLE_COLLECT;             \
        __VA_ARGS__;                          \
        CALLGRIND_TOGGLE_COLLECT;             \
        CALLGRIND_DUMP_STATS_AT(name);        \
    } while (0)

int main() {
    gq::test::Shape shape;
    shape.stores = 1500;
    shape.days = 365;
    shape.products = 4000;
    shape.orders = 120'000;
    shape.refund_percent = 25;
    const Dataset data = gq::test::make_dataset(shape, 0x5A1E5);
    const std::vector<gq::BasketLine> basket_a = gq::test::make_basket(data, 100'000, 11);
    const std::vector<gq::BasketLine> basket_b = gq::test::make_basket(data, 100'000, 12);
    const std::vector<std::uint16_t> region_a = gq::test::make_store_list(data, 50, 21);
    const std::vector<std::uint16_t> region_b = gq::test::make_store_list(data, 35, 22);

    gq::SalesStore store(shape.stores, shape.days);
    REGION("ingest", gq::test::ingest(store, data));

    std::int64_t rev[3] = {};
    REGION("revenue",
           rev[0] = store.revenue(0b0110, 0);           // pagos + enviados
           rev[1] = store.revenue(0b1111, 50'000);      // tudo acima de R$ 500
           rev[2] = store.revenue(0b1000, 120'000));    // cancelados grandes

    std::vector<std::int64_t> units_a, units_b;
    REGION("units_by_day",
           units_a = store.units_by_day(region_a);
           units_b = store.units_by_day(region_b));

    std::int64_t ref[3] = {};
    REGION("refunded_cents",
           ref[0] = store.refunded_cents(0, 364, 0);
           ref[1] = store.refunded_cents(31, 59, 0);
           ref[2] = store.refunded_cents(300, 120, 0) + store.refunded_cents(90, 180, 60'000));

    gq::BasketTotal bas_a, bas_b;
    REGION("basket_value",
           bas_a = store.basket_value(basket_a);
           bas_b = store.basket_value(basket_b));

    // Conferência com o gabarito (fora das regiões medidas).
    const gq::test::Model model(data);
    expect(store.order_count() == model.order_count(), "order_count()");
    expect(store.refund_count() == model.refund_count(), "refund_count()");
    expect(rev[0] == model.revenue(0b0110, 0), "revenue(0b0110, 0)");
    expect(rev[1] == model.revenue(0b1111, 50'000), "revenue(0b1111, 50000)");
    expect(rev[2] == model.revenue(0b1000, 120'000), "revenue(0b1000, 120000)");
    expect(units_a == model.units_by_day(region_a), "units_by_day(região A)");
    expect(units_b == model.units_by_day(region_b), "units_by_day(região B)");
    expect(ref[0] == model.refunded_cents(0, 364, 0), "refunded_cents(0, 364, 0)");
    expect(ref[1] == model.refunded_cents(31, 59, 0), "refunded_cents(31, 59, 0)");
    expect(ref[2] == model.refunded_cents(90, 180, 60'000), "refunded_cents(300, 120, 0) + refunded_cents(90, 180, 60000)");
    expect(bas_a == model.basket_value(basket_a), "basket_value(cesta A)");
    expect(bas_b == model.basket_value(basket_b), "basket_value(cesta B)");

    std::printf("revenue %lld %lld %lld\n", static_cast<long long>(rev[0]), static_cast<long long>(rev[1]),
                static_cast<long long>(rev[2]));
    std::printf("refunded_cents %lld %lld %lld\n", static_cast<long long>(ref[0]),
                static_cast<long long>(ref[1]), static_cast<long long>(ref[2]));
    std::printf("basket_value %lld/%u %lld/%u\n", static_cast<long long>(bas_a.total_cents), bas_a.unknown_lines,
                static_cast<long long>(bas_b.total_cents), bas_b.unknown_lines);
    if (g_failures > 0) {
        std::printf("FALHA: %d resultado(s) diferente(s) do gabarito\n", g_failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
