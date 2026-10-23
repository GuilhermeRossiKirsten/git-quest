// CONTRATO: Promotion e Checkout são tipos valor com movimento noexcept
#include <concepts>
#include <type_traits>

#include "promo/checkout.hpp"
#include "promo/promotion.hpp"

static_assert(std::copyable<promo::Promotion>, "Promotion precisa ser copiável e atribuível");
static_assert(std::is_nothrow_move_constructible_v<promo::Promotion>, "mover Promotion deve ser noexcept");
static_assert(std::is_nothrow_move_assignable_v<promo::Promotion>, "atribuir Promotion por movimento deve ser noexcept");
static_assert(std::copyable<promo::Checkout>, "Checkout precisa ser copiável e atribuível");
static_assert(std::is_nothrow_move_constructible_v<promo::Checkout>, "mover Checkout deve ser noexcept");
