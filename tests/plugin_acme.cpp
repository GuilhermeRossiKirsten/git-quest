// "Plugin" de um parceiro (Acme): tipos de promoção NOVOS, escritos fora do
// núcleo, usando só a API pública documentada no README:
//   - promo/promotion.hpp: promo::Promotion, promo::PromotionRule,
//     promo::discount/label/to_json e as promoções embutidas;
//   - promo/checkout.hpp:  promo::Checkout.
// Nenhum arquivo de src/ conhece estes tipos.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "promo/checkout.hpp"
#include "promo/promotion.hpp"
#include "support/minitest.hpp"

namespace acme {

// "Cashback: R$ 7,00 a cada R$ 100,00" (sobre o subtotal, sem frete).
class Cashback {
 public:
  Cashback(std::int64_t step_cents, std::int64_t back_cents) : step_(step_cents), back_(back_cents) {}

  std::int64_t discount(const promo::Cart& cart) const { return (cart.subtotal() / step_) * back_; }
  std::string label() const {
    return "Cashback: " + promo::format_brl(back_) + " a cada " + promo::format_brl(step_);
  }
  std::string to_json() const {
    return R"({"type":"acme.cashback","step":)" + std::to_string(step_) + R"(,"back":)" + std::to_string(back_) + "}";
  }

 private:
  std::int64_t step_;
  std::int64_t back_;
};

// "Combo pao + cafe": desconto fixo por par de itens levados juntos.
struct ComboDeal {
  std::string first_sku;
  std::string second_sku;
  std::int64_t off_per_pair = 0;

  std::int64_t discount(const promo::Cart& cart) const {
    const int a = cart.quantity_of(first_sku);
    const int b = cart.quantity_of(second_sku);
    return static_cast<std::int64_t>(a < b ? a : b) * off_per_pair;
  }
  std::string label() const { return "Combo " + first_sku + " + " + second_sku; }
  std::string to_json() const {
    return R"({"type":"acme.combo","skus":[")" + first_sku + R"(",")" + second_sku + R"("],"off":)" +
           std::to_string(off_per_pair) + "}";
  }
};

// Regra "grande" (512 bytes de tabela): promoções podem ter qualquer tamanho.
struct VolumeTable {
  std::array<std::int64_t, 64> off_by_units{};

  std::int64_t discount(const promo::Cart& cart) const {
    std::size_t units = 0;
    for (const auto& item : cart.items) units += static_cast<std::size_t>(item.qty);
    return off_by_units[units < off_by_units.size() ? units : off_by_units.size() - 1];
  }
  std::string label() const { return "Desconto por volume"; }
  std::string to_json() const { return R"({"type":"acme.volume"})"; }
};

}  // namespace acme

namespace {

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

promo::Checkout partner_checkout() {
  promo::Checkout c;
  c.add(promo::PercentOff(10, "bebidas"));
  c.add(acme::Cashback(10000, 700));
  c.add(promo::BuyXPayY("cafe", 3, 2));
  c.add(acme::ComboDeal{"pao", "cafe", 150});
  c.add(promo::FreeShipping(15000));
  return c;
}

const char* const kPartnerReceipt =
    "Subtotal: R$ 206,26\n"
    "Frete: R$ 24,90\n"
    "  10% em bebidas: -R$ 6,19\n"
    "  Cashback: R$ 7,00 a cada R$ 100,00: -R$ 14,00\n"
    "  Leve 3 pague 2: cafe: -R$ 37,80\n"
    "  Combo pao + cafe: -R$ 10,50\n"
    "  Frete grátis acima de R$ 150,00: -R$ 24,90\n"
    "Total: R$ 137,77\n";

const char* const kPartnerJson =
    R"([{"type":"percent_off","percent":10,"category":"bebidas"},)"
    R"({"type":"acme.cashback","step":10000,"back":700},)"
    R"({"type":"buy_x_pay_y","sku":"cafe","x":3,"y":2},)"
    R"({"type":"acme.combo","skus":["pao","cafe"],"off":150},)"
    R"({"type":"free_shipping","min_subtotal":15000}])";

}  // namespace

static_assert(promo::PromotionRule<acme::Cashback>);
static_assert(promo::PromotionRule<acme::ComboDeal>);
static_assert(promo::PromotionRule<acme::VolumeTable>);

TEST(plugin_promocao_direta, "Promotion p = acme::Cashback{...}: desconto, rótulo e JSON do plugin") {
  const promo::Promotion p = acme::Cashback(10000, 700);
  CHECK_EQ(promo::discount(p, market_cart()), std::int64_t{1400});
  CHECK_EQ(promo::label(p), std::string("Cashback: R$ 7,00 a cada R$ 100,00"));
  CHECK_EQ(promo::to_json(p), std::string(R"({"type":"acme.cashback","step":10000,"back":700})"));
  const promo::Promotion combo = acme::ComboDeal{"pao", "cafe", 150};
  CHECK_EQ(promo::discount(combo, market_cart()), std::int64_t{1050});
  CHECK_EQ(promo::label(combo), std::string("Combo pao + cafe"));
}

TEST(plugin_caixa_misto, "Checkout com promoções embutidas e do plugin: total, cupom e exportação") {
  const auto c = partner_checkout();
  CHECK_EQ(c.size(), std::size_t{5});
  CHECK_EQ(c.total(market_cart()), std::int64_t{13777});
  CHECK_EQ(c.receipt(market_cart()), std::string(kPartnerReceipt));
  CHECK_EQ(c.export_json(), std::string(kPartnerJson));
}

TEST(plugin_regra_grande, "regra de 512 bytes funciona, copia e exporta como as outras") {
  acme::VolumeTable table;
  table.off_by_units[33] = 999;
  promo::Checkout c;
  c.add(table);
  promo::Checkout copy = c;
  CHECK_EQ(copy.total(market_cart()), std::int64_t{23116 - 999});
  CHECK_EQ(copy.export_json(), std::string(R"([{"type":"acme.volume"}])"));
  promo::Promotion p = table;
  table.off_by_units[33] = 1;  // a Promotion guardou uma cópia, não uma referência
  CHECK_EQ(promo::discount(p, market_cart()), std::int64_t{999});
}

TEST(valor_plugin_copias, "cópias de caixas e vetores com promoções do plugin são independentes") {
  auto original = std::make_unique<promo::Checkout>(partner_checkout());
  promo::Checkout pdv2 = *original;
  auto snapshot = original->promotions();
  original.reset();
  CHECK_EQ(pdv2.receipt(market_cart()), std::string(kPartnerReceipt));
  CHECK_EQ(snapshot.size(), std::size_t{5});
  if (snapshot.size() != 5) return;
  CHECK_EQ(promo::label(snapshot[1]), std::string("Cashback: R$ 7,00 a cada R$ 100,00"));
  CHECK_EQ(promo::label(snapshot[3]), std::string("Combo pao + cafe"));
  snapshot[3] = promo::FixedOff(1, 0);
  CHECK_EQ(pdv2.export_json(), std::string(kPartnerJson));

  std::vector<promo::Promotion> mixed{acme::Cashback(5000, 100), promo::FreeShipping(0),
                                      acme::ComboDeal{"agua", "suco", 30}};
  auto other = mixed;
  other[0] = promo::PercentOff(5, "padaria");
  CHECK_EQ(promo::to_json(mixed[0]), std::string(R"({"type":"acme.cashback","step":5000,"back":100})"));
  CHECK_EQ(promo::discount(mixed[2], market_cart()), std::int64_t{120});
}
