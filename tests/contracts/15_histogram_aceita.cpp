// CONTRATO: Histogram aceita chaves Hashable por valor
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

static_assert(probe::CanMakeHistogram<std::string>, "Histogram<std::string> deve existir");
static_assert(probe::CanMakeHistogram<int>, "Histogram<int> deve existir");
static_assert(probe::CanMakeHistogram<Sku>, "Histogram<Sku> deve existir");
