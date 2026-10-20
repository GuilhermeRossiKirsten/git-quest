// CONTRATO: NumericRange rejeita não-ranges e ranges de não-números
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

static_assert(!gq::NumericRange<int>, "int (não é range) NÃO deve ser NumericRange");
static_assert(!gq::NumericRange<std::string>, "std::string (range de char) NÃO deve ser NumericRange");
static_assert(!gq::NumericRange<std::vector<bool>>, "vector<bool> NÃO deve ser NumericRange");
static_assert(!gq::NumericRange<std::vector<std::string>>, "vector<string> NÃO deve ser NumericRange");
