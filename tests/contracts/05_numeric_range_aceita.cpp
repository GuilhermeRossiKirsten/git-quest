// CONTRATO: NumericRange aceita vector, list, array e span de números
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

static_assert(gq::NumericRange<std::vector<double>>, "vector<double> deve ser NumericRange");
static_assert(gq::NumericRange<std::list<int>>, "list<int> deve ser NumericRange");
static_assert(gq::NumericRange<std::array<float, 3>>, "array<float, 3> deve ser NumericRange");
static_assert(gq::NumericRange<std::span<const int>>, "span<const int> deve ser NumericRange");
