// Segundo "plugin": uma regra que conta quantas instâncias de si mesma estão
// vivas. Serve para medir o ciclo de vida das cópias guardadas pela Promotion.
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "promo/checkout.hpp"
#include "promo/promotion.hpp"
#include "support/alloc_counter.hpp"
#include "support/minitest.hpp"

namespace audit {

class TrackedRule {
 public:
  static inline int live = 0;

  explicit TrackedRule(int id) : id_(id) { ++live; }
  TrackedRule(const TrackedRule& other) : id_(other.id_) { ++live; }
  TrackedRule(TrackedRule&& other) noexcept : id_(other.id_) { ++live; }
  TrackedRule& operator=(const TrackedRule&) = default;
  TrackedRule& operator=(TrackedRule&&) noexcept = default;
  ~TrackedRule() { --live; }

  std::int64_t discount(const promo::Cart&) const { return id_; }
  std::string label() const { return "Regra rastreada #" + std::to_string(id_); }
  std::string to_json() const { return R"({"type":"audit.tracked","id":)" + std::to_string(id_) + "}"; }

 private:
  int id_;
};

}  // namespace audit

TEST(valor_plugin_vida_util, "toda cópia de regra guardada é destruída exatamente uma vez") {
  audit::TrackedRule::live = 0;
  {
    promo::Promotion a = audit::TrackedRule(1);
    CHECK(audit::TrackedRule::live >= 1);
    promo::Promotion b = a;
    b = audit::TrackedRule(2);
    CHECK_EQ(promo::label(a), std::string("Regra rastreada #1"));
    CHECK_EQ(promo::label(b), std::string("Regra rastreada #2"));
    promo::Promotion c = std::move(b);
    b = c;
    std::vector<promo::Promotion> v;
    for (int i = 0; i < 20; ++i) v.push_back(audit::TrackedRule(100 + i));
    v.erase(v.begin() + 3, v.begin() + 9);
    promo::Checkout checkout;
    for (const auto& p : v) checkout.add(p);
    promo::Checkout copy = checkout;
    copy = checkout;
    auto snapshot = copy.promotions();
    CHECK_EQ(promo::label(snapshot[3]), std::string("Regra rastreada #109"));
    CHECK_EQ(promo::to_json(c), std::string(R"({"type":"audit.tracked","id":2})"));
  }
  CHECK_EQ(audit::TrackedRule::live, 0);
}

TEST(mem_plugin_alocacoes, "regra do plugin: construir/copiar <= 2 alocações, mover 0") {
  audit::TrackedRule rule(7);
  long built = 0;
  long copied = 0;
  long moved = 0;
  {
    gq::AllocCounter counter;
    promo::Promotion a = rule;
    built = counter.count();
    counter.reset();
    promo::Promotion b = a;
    copied = counter.count();
    counter.reset();
    promo::Promotion c = std::move(a);
    a = std::move(c);
    moved = counter.count();
  }
  CHECK_LE(built, 2L);
  CHECK_LE(copied, 2L);
  CHECK_EQ(moved, 0L);
}
