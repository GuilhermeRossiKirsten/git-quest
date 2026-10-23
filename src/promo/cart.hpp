#pragma once
// Carrinho de compras e formatação de dinheiro. Valores SEMPRE em centavos
// (std::int64_t): nada de double para dinheiro.

#include <cstdint>
#include <string>
#include <vector>

namespace promo {

struct Item {
  std::string sku;
  std::string category;
  std::int64_t unit_cents = 0;
  int qty = 0;
};

struct Cart {
  std::vector<Item> items;
  std::int64_t shipping_cents = 0;

  // Soma de unit_cents * qty de todos os itens (sem o frete).
  std::int64_t subtotal() const;
  // Mesma soma, só com os itens da categoria indicada.
  std::int64_t category_subtotal(const std::string& category) const;
  // Quantidade total do SKU no carrinho (0 se ele não estiver lá).
  int quantity_of(const std::string& sku) const;
  // Preço unitário do SKU (0 se ele não estiver no carrinho).
  std::int64_t unit_price_of(const std::string& sku) const;
};

// 123456 -> "R$ 1.234,56";  -150 -> "-R$ 1,50";  0 -> "R$ 0,00"
std::string format_brl(std::int64_t cents);

}  // namespace promo
