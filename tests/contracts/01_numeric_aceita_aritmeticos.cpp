// CONTRATO: Numeric aceita int, long long, unsigned, float e double
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

static_assert(gq::Numeric<int>, "int deve ser Numeric");
static_assert(gq::Numeric<long long>, "long long deve ser Numeric");
static_assert(gq::Numeric<unsigned>, "unsigned deve ser Numeric");
static_assert(gq::Numeric<float>, "float deve ser Numeric");
static_assert(gq::Numeric<double>, "double deve ser Numeric");
