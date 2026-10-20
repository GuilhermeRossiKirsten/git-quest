// CONTRATO: Numeric rejeita tipos de caractere (char, wchar_t, char8_t, char16_t, char32_t)
#include <array>
#include <complex>
#include <concepts>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gq/metrics.hpp"
#include "support/probes.hpp"
#include "support/types.hpp"

using test_types::HashWithoutEquality;
using test_types::Point;
using test_types::Sku;

static_assert(!gq::Numeric<char>, "char NÃO deve ser Numeric");
static_assert(!gq::Numeric<wchar_t>, "wchar_t NÃO deve ser Numeric");
static_assert(!gq::Numeric<char8_t>, "char8_t NÃO deve ser Numeric");
static_assert(!gq::Numeric<char16_t>, "char16_t NÃO deve ser Numeric");
static_assert(!gq::Numeric<char32_t>, "char32_t NÃO deve ser Numeric");
