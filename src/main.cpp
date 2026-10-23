// Demonstração do caixa: `./build/caixa` (start.sh roda este programa).
#include <cstdio>

#include "promo/checkout.hpp"

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // nada se perde se o programa abortar

  promo::Cart cart;
  cart.items = {
      {"cafe", "mercearia", 1890, 7},
      {"suco", "bebidas", 799, 4},
      {"agua", "bebidas", 250, 12},
      {"pao", "padaria", 120, 10},
  };
  cart.shipping_cents = 2490;

  promo::Checkout loja;
  loja.add(promo::PercentOff(10, "bebidas"));
  loja.add(promo::FixedOff(1500, 10000));
  loja.add(promo::BuyXPayY("cafe", 3, 2));
  loja.add(promo::FreeShipping(15000));

  std::printf("== Caixa 1\n%s", loja.receipt(cart).c_str());
  std::printf("== Exportação\n%s\n", loja.export_json().c_str());

  // Abre um segundo PDV com a mesma configuração.
  promo::Checkout pdv2 = loja;
  std::printf("== Caixa 2 (cópia do caixa 1)\n%s", pdv2.receipt(cart).c_str());
  return 0;
}
