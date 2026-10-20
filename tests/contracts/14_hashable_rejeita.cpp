// CONTRATO: Hashable rejeita tipos sem std::hash ou sem ==
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

static_assert(!gq::Hashable<Point>, "Point (sem std::hash) NÃO deve ser Hashable");
static_assert(!gq::Hashable<std::vector<int>>, "vector<int> (sem std::hash) NÃO deve ser Hashable");
static_assert(!gq::Hashable<HashWithoutEquality>, "tipo sem operador == NÃO deve ser Hashable");
