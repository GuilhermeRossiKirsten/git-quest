// CONTRATO: tipos arbitrários (int, string, Cart, ponteiros) não viram Promotion
#include "support/probes.hpp"

static_assert(probe::Rejected<int>, "int não é uma regra de promoção");
static_assert(probe::Rejected<double>, "double não é uma regra de promoção");
static_assert(probe::Rejected<std::string>, "std::string não é uma regra de promoção");
static_assert(probe::Rejected<promo::Cart>, "promo::Cart não é uma regra de promoção");
static_assert(probe::Rejected<samples::GoodRule*>, "ponteiro para regra não é uma regra (guarde valores)");
