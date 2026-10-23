// Testes do núcleo, só com as promoções embutidas. Prefixos dos ids:
//   ref_    saídas de referência (o comportamento atual não pode mudar)
//   valor_  semântica de valor: cópias independentes, sem fatiamento
//   mem_    alocações (vazamentos são verificados pelo LeakSanitizer em todos)
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "promo/checkout.hpp"
#include "promo/promotion.hpp"
#include "support/alloc_counter.hpp"
#include "support/minitest.hpp"

namespace {

// O carrinho da demonstração (src/main.cpp).
promo::Cart market_cart() {
  promo::Cart cart;
  cart.items = {
      {"cafe", "mercearia", 1890, 7},
      {"suco", "bebidas", 799, 4},
      {"agua", "bebidas", 250, 12},
      {"pao", "padaria", 120, 10},
  };
  cart.shipping_cents = 2490;
  return cart;
}

promo::Cart small_cart() {
  promo::Cart cart;
  cart.items = {{"pao", "padaria", 120, 5}, {"suco", "bebidas", 799, 1}};
  cart.shipping_cents = 990;
  return cart;
}

promo::Checkout store_checkout() {
  promo::Checkout c;
  c.add(promo::PercentOff(10, "bebidas"));
  c.add(promo::FixedOff(1500, 10000));
  c.add(promo::BuyXPayY("cafe", 3, 2));
  c.add(promo::FreeShipping(15000));
  return c;
}

const char* const kStoreReceipt =
    "Subtotal: R$ 206,26\n"
    "Frete: R$ 24,90\n"
    "  10% em bebidas: -R$ 6,19\n"
    "  R$ 15,00 de desconto acima de R$ 100,00: -R$ 15,00\n"
    "  Leve 3 pague 2: cafe: -R$ 37,80\n"
    "  Frete grátis acima de R$ 150,00: -R$ 24,90\n"
    "Total: R$ 147,27\n";

const char* const kStoreJson =
    R"([{"type":"percent_off","percent":10,"category":"bebidas"},)"
    R"({"type":"fixed_off","cents":1500,"min_subtotal":10000},)"
    R"({"type":"buy_x_pay_y","sku":"cafe","x":3,"y":2},)"
    R"({"type":"free_shipping","min_subtotal":15000}])";

}  // namespace

// ===========================================================================
// (1) Saídas de referência
// ===========================================================================

TEST(ref_percentual, "PercentOff: desconto, cupom e JSON de referência") {
  const auto cart = market_cart();
  promo::Checkout c;
  c.add(promo::PercentOff(10, "bebidas"));
  CHECK_EQ(c.total(cart), std::int64_t{22497});
  CHECK_EQ(c.receipt(cart), std::string("Subtotal: R$ 206,26\nFrete: R$ 24,90\n"
                                        "  10% em bebidas: -R$ 6,19\nTotal: R$ 224,97\n"));
  CHECK_EQ(c.export_json(), std::string(R"([{"type":"percent_off","percent":10,"category":"bebidas"}])"));
}

TEST(ref_valor_fixo, "FixedOff: acima e abaixo do mínimo, e limitado ao subtotal") {
  promo::Checkout c;
  c.add(promo::FixedOff(1500, 10000));
  CHECK_EQ(c.total(market_cart()), std::int64_t{21616});
  CHECK_EQ(c.receipt(small_cart()), std::string("Subtotal: R$ 13,99\nFrete: R$ 9,90\nTotal: R$ 23,89\n"));
  CHECK_EQ(c.export_json(), std::string(R"([{"type":"fixed_off","cents":1500,"min_subtotal":10000}])"));

  auto cart = small_cart();
  cart.shipping_cents = 0;
  promo::Checkout big;
  big.add(promo::FixedOff(5000, 0));
  CHECK_EQ(big.total(cart), std::int64_t{0});
  CHECK_EQ(big.receipt(cart), std::string("Subtotal: R$ 13,99\n  R$ 50,00 de desconto: -R$ 13,99\nTotal: R$ 0,00\n"));
}

TEST(ref_leve_pague, "BuyXPayY: grupos completos, quantidade insuficiente e regra degenerada") {
  promo::Checkout c;
  c.add(promo::BuyXPayY("cafe", 3, 2));
  c.add(promo::BuyXPayY("pao", 4, 4));  // x == y: nunca desconta
  CHECK_EQ(c.total(market_cart()), std::int64_t{19336});
  CHECK_EQ(c.receipt(market_cart()), std::string("Subtotal: R$ 206,26\nFrete: R$ 24,90\n"
                                                 "  Leve 3 pague 2: cafe: -R$ 37,80\nTotal: R$ 193,36\n"));
  promo::Checkout few;
  few.add(promo::BuyXPayY("suco", 5, 3));
  CHECK_EQ(few.total(small_cart()), std::int64_t{2389});
  CHECK_EQ(c.export_json(), std::string(R"([{"type":"buy_x_pay_y","sku":"cafe","x":3,"y":2},)"
                                        R"({"type":"buy_x_pay_y","sku":"pao","x":4,"y":4}])"));
}

TEST(ref_frete_gratis, "FreeShipping: zera o frete acima do mínimo e exporta o próprio JSON") {
  promo::Checkout c;
  c.add(promo::FreeShipping(15000));
  CHECK_EQ(c.total(market_cart()), std::int64_t{20626});
  CHECK_EQ(c.total(small_cart()), std::int64_t{2389});
  CHECK_EQ(c.receipt(market_cart()), std::string("Subtotal: R$ 206,26\nFrete: R$ 24,90\n"
                                                 "  Frete grátis acima de R$ 150,00: -R$ 24,90\nTotal: R$ 206,26\n"));
  CHECK_EQ(c.export_json(), std::string(R"([{"type":"free_shipping","min_subtotal":15000}])"));
}

TEST(ref_caixa_completo, "as quatro promoções juntas: total, cupom e exportação da loja") {
  const auto c = store_checkout();
  CHECK_EQ(c.size(), std::size_t{4});
  CHECK_EQ(c.total(market_cart()), std::int64_t{14727});
  CHECK_EQ(c.receipt(market_cart()), std::string(kStoreReceipt));
  CHECK_EQ(c.export_json(), std::string(kStoreJson));
}

TEST(ref_funcoes_livres, "promo::discount/label/to_json direto em cada promoção embutida") {
  const auto cart = market_cart();
  CHECK_EQ(promo::discount(promo::PercentOff(25, "padaria"), cart), std::int64_t{300});
  CHECK_EQ(promo::label(promo::PercentOff(25, "padaria")), std::string("25% em padaria"));
  CHECK_EQ(promo::discount(promo::FixedOff(123456, 0), cart), std::int64_t{20626});
  CHECK_EQ(promo::label(promo::FixedOff(123456, 0)), std::string("R$ 1.234,56 de desconto"));
  CHECK_EQ(promo::discount(promo::BuyXPayY("agua", 6, 5), cart), std::int64_t{500});
  CHECK_EQ(promo::label(promo::BuyXPayY("agua", 6, 5)), std::string("Leve 6 pague 5: agua"));
  CHECK_EQ(promo::discount(promo::FreeShipping(30000), cart), std::int64_t{0});
  CHECK_EQ(promo::label(promo::FreeShipping(30000)), std::string("Frete grátis acima de R$ 300,00"));
  CHECK_EQ(promo::to_json(promo::PercentOff(25, "padaria")),
           std::string(R"({"type":"percent_off","percent":25,"category":"padaria"})"));
  CHECK_EQ(promo::to_json(promo::FixedOff(123456, 0)), std::string(R"({"type":"fixed_off","cents":123456,"min_subtotal":0})"));
  CHECK_EQ(promo::to_json(promo::BuyXPayY("agua", 6, 5)), std::string(R"({"type":"buy_x_pay_y","sku":"agua","x":6,"y":5})"));
  CHECK_EQ(promo::to_json(promo::FreeShipping(30000)), std::string(R"({"type":"free_shipping","min_subtotal":30000})"));
}

TEST(ref_caixa_vazio, "caixa sem promoções e carrinho vazio") {
  promo::Checkout none;
  CHECK_EQ(none.size(), std::size_t{0});
  CHECK_EQ(none.export_json(), std::string("[]"));
  CHECK_EQ(none.receipt(promo::Cart{}), std::string("Subtotal: R$ 0,00\nTotal: R$ 0,00\n"));
  const auto c = store_checkout();
  CHECK_EQ(c.receipt(promo::Cart{}), std::string("Subtotal: R$ 0,00\nTotal: R$ 0,00\n"));
  CHECK(c.promotions().size() == 4);
}

// ===========================================================================
// (3) Semântica de valor
// ===========================================================================

TEST(valor_sem_fatiamento, "Promotion p = BuyXPayY(...) guarda a promoção inteira (sem fatiamento)") {
  const promo::Promotion p = promo::BuyXPayY("cafe", 3, 2);
  CHECK_EQ(promo::label(p), std::string("Leve 3 pague 2: cafe"));
  CHECK_EQ(promo::discount(p, market_cart()), std::int64_t{3780});
  CHECK_EQ(promo::to_json(p), std::string(R"({"type":"buy_x_pay_y","sku":"cafe","x":3,"y":2})"));
  const promo::Promotion q = promo::PercentOff(10, "bebidas");
  CHECK_EQ(promo::label(q), std::string("10% em bebidas"));
}

TEST(valor_copia_independente, "copiar e reatribuir uma Promotion não afeta o original") {
  promo::Promotion a = promo::FixedOff(1500, 10000);
  promo::Promotion b = a;
  b = promo::PercentOff(10, "bebidas");
  CHECK_EQ(promo::label(a), std::string("R$ 15,00 de desconto acima de R$ 100,00"));
  CHECK_EQ(promo::label(b), std::string("10% em bebidas"));

  auto owner = std::make_unique<promo::Promotion>(promo::BuyXPayY("agua", 6, 5));
  promo::Promotion copy = *owner;
  owner.reset();  // a cópia não pode depender da vida do original
  CHECK_EQ(promo::discount(copy, market_cart()), std::int64_t{500});

  const promo::Promotion& alias = a;
  a = alias;  // auto-atribuição
  CHECK_EQ(promo::label(a), std::string("R$ 15,00 de desconto acima de R$ 100,00"));
  b = std::move(a);
  a = promo::FreeShipping(15000);  // objeto movido volta a ser usável
  CHECK_EQ(promo::label(a), std::string("Frete grátis acima de R$ 150,00"));
  CHECK_EQ(promo::label(b), std::string("R$ 15,00 de desconto acima de R$ 100,00"));
}

TEST(valor_vetor_heterogeneo, "std::vector<Promotion> por valor com tipos diferentes") {
  std::vector<promo::Promotion> v{promo::PercentOff(10, "bebidas"), promo::FixedOff(1500, 10000),
                                  promo::BuyXPayY("cafe", 3, 2), promo::FreeShipping(15000)};
  std::int64_t sum = 0;
  for (const auto& p : v) sum += promo::discount(p, market_cart());
  CHECK_EQ(sum, std::int64_t{8389});

  auto w = v;
  w[0] = promo::FixedOff(1, 0);
  w.pop_back();
  CHECK_EQ(v.size(), std::size_t{4});
  CHECK_EQ(promo::label(v[0]), std::string("10% em bebidas"));
  CHECK_EQ(promo::label(v[3]), std::string("Frete grátis acima de R$ 150,00"));
  CHECK_EQ(promo::label(w[2]), std::string("Leve 3 pague 2: cafe"));
}

TEST(valor_copia_do_caixa, "copiar um Checkout gera um caixa independente e completo") {
  auto original = std::make_unique<promo::Checkout>(store_checkout());
  promo::Checkout pdv2 = *original;
  pdv2.add(promo::FixedOff(100, 0));
  CHECK_EQ(original->size(), std::size_t{4});
  CHECK_EQ(original->receipt(market_cart()), std::string(kStoreReceipt));
  CHECK_EQ(original->export_json(), std::string(kStoreJson));

  promo::Checkout pdv3;
  pdv3 = *original;  // atribuição por cópia
  original.reset();
  CHECK_EQ(pdv3.receipt(market_cart()), std::string(kStoreReceipt));
  CHECK_EQ(pdv2.size(), std::size_t{5});
  CHECK_EQ(pdv2.total(market_cart()), std::int64_t{14627});
}

TEST(valor_promocoes_snapshot, "Checkout::promotions() devolve cópias completas e independentes") {
  const auto c = store_checkout();
  auto snapshot = c.promotions();
  CHECK_EQ(snapshot.size(), std::size_t{4});
  if (snapshot.size() != 4) return;
  CHECK_EQ(promo::label(snapshot[0]), std::string("10% em bebidas"));
  CHECK_EQ(promo::label(snapshot[2]), std::string("Leve 3 pague 2: cafe"));
  CHECK_EQ(promo::to_json(snapshot[3]), std::string(R"({"type":"free_shipping","min_subtotal":15000})"));
  snapshot[1] = promo::PercentOff(50, "padaria");
  snapshot.erase(snapshot.begin());
  CHECK_EQ(c.export_json(), std::string(kStoreJson));
  CHECK_EQ(c.total(market_cart()), std::int64_t{14727});
}

// ===========================================================================
// (4) Alocações
// ===========================================================================

TEST(mem_mover_sem_alocar, "mover uma Promotion não aloca (e o destino fica com a promoção)") {
  promo::Promotion a = promo::BuyXPayY("cafe", 3, 2);
  long moves = 0;
  {
    gq::AllocCounter counter;
    promo::Promotion b = std::move(a);
    a = std::move(b);
    moves = counter.count();
  }
  CHECK_EQ(moves, 0L);
  CHECK_EQ(promo::label(a), std::string("Leve 3 pague 2: cafe"));
}

TEST(mem_construir_e_copiar, "construir ou copiar uma Promotion faz no máximo 2 alocações") {
  promo::BuyXPayY rule("cafe", 3, 2);
  long built = 0;
  long copied = 0;
  {
    gq::AllocCounter counter;
    promo::Promotion a = rule;
    built = counter.count();
    counter.reset();
    promo::Promotion b = a;
    copied = counter.count();
    counter.reset();
    CHECK_EQ(promo::discount(b, market_cart()), std::int64_t{3780});
  }
  CHECK_LE(built, 2L);
  CHECK_LE(copied, 2L);
}

TEST(mem_vetor_crescendo, "100 push_back sem reserve: realocar o vetor não copia as promoções") {
  std::vector<promo::Promotion> v;
  long allocations = 0;
  {
    gq::AllocCounter counter;
    for (int i = 0; i < 100; ++i) v.push_back(promo::Promotion(promo::PercentOff(i, "bebidas")));
    allocations = counter.count();
  }
  // ~100 promoções + ~8 realocações do vetor; copiar a cada realocação passa de 230.
  CHECK_LE(allocations, 150L);
  CHECK_EQ(promo::label(v[57]), std::string("57% em bebidas"));
}

TEST(mem_total_sem_alocar, "Checkout::total() não aloca memória") {
  promo::Checkout c;
  for (int i = 0; i < 10; ++i) {
    c.add(promo::PercentOff(1, "bebidas"));
    c.add(promo::FixedOff(10, 0));
    c.add(promo::BuyXPayY("cafe", 7, 6));
    c.add(promo::FreeShipping(99999999));
  }
  const auto cart = market_cart();
  std::int64_t total = 0;
  long allocations = 0;
  {
    gq::AllocCounter counter;
    total = c.total(cart);
    allocations = counter.count();
  }
  CHECK_EQ(allocations, 0L);
  CHECK_EQ(total, std::int64_t{23116 - 10 * (61 + 10 + 1890)});
}
