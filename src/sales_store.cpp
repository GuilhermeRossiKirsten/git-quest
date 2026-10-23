// sales_store.cpp — implementação do painel de vendas (código legado).
//
// Funciona e dá os resultados certos. O problema é o custo: com a rede inteira
// carregada, as consultas do painel ficaram lentas demais. Veja o README.
#include "sales_store.h"

#include <stdexcept>

namespace gq {

SalesStore::SalesStore(std::uint16_t stores, std::uint16_t days)
    : stores_(stores), days_(days), units_(static_cast<std::size_t>(stores) * days, 0) {}

void SalesStore::load_catalog(std::span<const Product> products) {
    for (const Product& p : products) {
        catalog_[p.sku] = p;
    }
}

void SalesStore::add_order(const Order& order) {
    if (order.store >= stores_ || order.day >= days_) {
        throw std::out_of_range("add_order: loja ou dia fora do intervalo");
    }
    auto rec = std::make_unique<OrderRecord>();
    rec->id = order.id;
    rec->customer_id = order.customer_id;
    rec->sku = order.sku;
    rec->store = order.store;
    rec->day = order.day;
    rec->quantity = order.quantity;
    rec->status = order.status;
    rec->coupon = order.coupon;
    rec->shipping_address = order.shipping_address;
    rec->notes = order.notes;
    rec->amount_cents = order.amount_cents;

    units_[static_cast<std::size_t>(order.store) * days_ + order.day] += order.quantity;
    order_by_id_[order.id] = rec.get();
    orders_.push_back(std::move(rec));
}

void SalesStore::add_refund(const Refund& refund) {
    refunds_.push_back(refund);
}

std::size_t SalesStore::order_count() const {
    return orders_.size();
}

std::size_t SalesStore::refund_count() const {
    return refunds_.size();
}

std::optional<Order> SalesStore::find_order(std::uint64_t id) const {
    auto it = order_by_id_.find(id);
    if (it == order_by_id_.end()) {
        return std::nullopt;
    }
    const OrderRecord& r = *it->second;
    Order o;
    o.id = r.id;
    o.customer_id = r.customer_id;
    o.sku = r.sku;
    o.store = r.store;
    o.day = r.day;
    o.quantity = r.quantity;
    o.amount_cents = r.amount_cents;
    o.status = r.status;
    o.coupon = r.coupon;
    o.shipping_address = r.shipping_address;
    o.notes = r.notes;
    return o;
}

std::optional<Product> SalesStore::find_product(std::uint32_t sku) const {
    auto it = catalog_.find(sku);
    if (it == catalog_.end()) {
        return std::nullopt;
    }
    return it->second;
}

// --- consultas do painel -------------------------------------------------

std::int64_t SalesStore::revenue(std::uint8_t status_mask, std::int64_t min_amount_cents) const {
    std::int64_t total = 0;
    for (const auto& rec : orders_) {
        const unsigned bit = 1u << static_cast<unsigned>(rec->status);
        if ((status_mask & bit) != 0 && rec->amount_cents >= min_amount_cents) {
            total += rec->amount_cents;
        }
    }
    return total;
}

std::vector<std::int64_t> SalesStore::units_by_day(std::span<const std::uint16_t> stores) const {
    std::vector<std::int64_t> totals(days_, 0);
    for (std::uint16_t day = 0; day < days_; ++day) {
        std::int64_t sum = 0;
        for (std::uint16_t store : stores) {
            sum += units_.at(static_cast<std::size_t>(store) * days_ + day);
        }
        totals[day] = sum;
    }
    return totals;
}

std::int64_t SalesStore::refunded_cents(std::uint16_t first_day, std::uint16_t last_day,
                                        std::int64_t min_amount_cents) const {
    std::int64_t total = 0;
    for (const Refund& r : refunds_) {
        if (r.day >= first_day && r.day <= last_day && r.amount_cents >= min_amount_cents) {
            total += r.amount_cents;
        }
    }
    return total;
}

BasketTotal SalesStore::basket_value(std::span<const BasketLine> lines) const {
    BasketTotal result;
    for (const BasketLine& line : lines) {
        auto it = catalog_.find(line.sku);
        if (it == catalog_.end()) {
            ++result.unknown_lines;
            continue;
        }
        result.total_cents += static_cast<std::int64_t>(line.quantity) * it->second.price_cents;
    }
    return result;
}

}  // namespace gq
