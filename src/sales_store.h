// sales_store.h — painel de vendas em memória da rede "Mercado Boa Vista".
//
// API PÚBLICA (fixa — os testes e o avaliador dependem dela; veja o README):
//
//   SalesStore(stores, days)                  lojas 0..stores-1, dias 0..days-1
//   void load_catalog(span<const Product>)    chamado uma vez, antes de tudo; SKUs únicos
//   void add_order(const Order&)              ids únicos; store < stores, day < days
//   void add_refund(const Refund&)            devolução (pode citar um pedido inexistente)
//
//   size_t order_count() const
//   size_t refund_count() const
//   optional<Order>   find_order(id) const    o pedido completo, como foi inserido
//   optional<Product> find_product(sku) const o produto completo, como foi carregado
//
//   Consultas quentes (o avaliador mede o custo de memória de cada uma):
//   int64_t revenue(status_mask, min_amount_cents) const
//       soma de amount_cents dos pedidos com status no mask (bit 1 << status)
//       e amount_cents >= min_amount_cents
//   vector<int64_t> units_by_day(span<const uint16_t> stores) const
//       vetor com `days` posições: total de unidades vendidas em cada dia,
//       somando apenas as lojas listadas (uma loja repetida conta de novo)
//   int64_t refunded_cents(first_day, last_day, min_amount_cents) const
//       soma das devoluções com first_day <= day <= last_day e
//       amount_cents >= min_amount_cents (intervalo vazio => 0)
//   BasketTotal basket_value(span<const BasketLine>) const
//       soma de quantity * price_cents de cada linha; linhas com SKU fora do
//       catálogo não somam nada e são contadas em unknown_lines
//
// Os tipos Status, Order, Refund, Product, BasketLine e BasketTotal também são
// públicos e fixos. A parte privada de SalesStore é sua.
#ifndef GITQUEST_SALES_STORE_H
#define GITQUEST_SALES_STORE_H

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gq {

enum class Status : std::uint8_t { Pending = 0, Paid = 1, Shipped = 2, Cancelled = 3 };

struct Order {
    std::uint64_t id = 0;
    std::uint32_t customer_id = 0;
    std::uint32_t sku = 0;
    std::uint16_t store = 0;
    std::uint16_t day = 0;
    std::uint32_t quantity = 0;
    std::int64_t amount_cents = 0;
    Status status = Status::Pending;
    std::string coupon;
    std::string shipping_address;
    std::string notes;

    bool operator==(const Order&) const = default;
};

struct Refund {
    std::uint64_t order_id = 0;
    std::uint16_t day = 0;
    std::int64_t amount_cents = 0;
    std::uint8_t reason = 0;
    std::string comment;

    bool operator==(const Refund&) const = default;
};

struct Product {
    std::uint32_t sku = 0;
    std::int64_t price_cents = 0;
    std::string name;
    std::string category;
    std::string supplier;

    bool operator==(const Product&) const = default;
};

struct BasketLine {
    std::uint32_t sku = 0;
    std::uint32_t quantity = 0;
};

struct BasketTotal {
    std::int64_t total_cents = 0;
    std::uint32_t unknown_lines = 0;

    bool operator==(const BasketTotal&) const = default;
};

class SalesStore {
public:
    SalesStore(std::uint16_t stores, std::uint16_t days);

    void load_catalog(std::span<const Product> products);
    void add_order(const Order& order);
    void add_refund(const Refund& refund);

    std::size_t order_count() const;
    std::size_t refund_count() const;
    std::optional<Order> find_order(std::uint64_t id) const;
    std::optional<Product> find_product(std::uint32_t sku) const;

    std::int64_t revenue(std::uint8_t status_mask, std::int64_t min_amount_cents) const;
    std::vector<std::int64_t> units_by_day(std::span<const std::uint16_t> stores) const;
    std::int64_t refunded_cents(std::uint16_t first_day, std::uint16_t last_day,
                                std::int64_t min_amount_cents) const;
    BasketTotal basket_value(std::span<const BasketLine> lines) const;

private:
    // Registro interno de um pedido, como o sistema antigo gravava no banco.
    // Os campos foram sendo acrescentados ao longo dos anos (o valor em
    // centavos veio na migração de moeda, por isso está lá no fim).
    struct OrderRecord {
        std::uint64_t id = 0;
        std::uint32_t customer_id = 0;
        std::uint32_t sku = 0;
        std::uint16_t store = 0;
        std::uint16_t day = 0;
        std::uint32_t quantity = 0;
        Status status = Status::Pending;
        std::uint32_t revision = 0;  // quantas vezes o registro foi regravado
        std::string coupon;
        std::string shipping_address;
        std::string notes;
        std::int64_t amount_cents = 0;
    };

    std::uint16_t stores_;
    std::uint16_t days_;

    // Pedidos na ordem de chegada; cada um tem seu próprio bloco no heap.
    std::vector<std::unique_ptr<OrderRecord>> orders_;
    std::unordered_map<std::uint64_t, const OrderRecord*> order_by_id_;

    // Unidades vendidas: uma linha por loja, uma coluna por dia
    // (units_[store * days_ + day]).
    std::vector<std::int64_t> units_;

    // Devoluções na ordem de chegada.
    std::list<Refund> refunds_;

    // Catálogo indexado por SKU.
    std::map<std::uint32_t, Product> catalog_;
};

}  // namespace gq

#endif
