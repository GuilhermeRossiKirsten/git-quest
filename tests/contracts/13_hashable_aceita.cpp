// CONTRATO: Hashable aceita int, std::string e tipo com std::hash especializado
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

static_assert(gq::Hashable<int>, "int deve ser Hashable");
static_assert(gq::Hashable<std::string>, "std::string deve ser Hashable");
static_assert(gq::Hashable<Sku>, "Sku (com std::hash<Sku> e ==) deve ser Hashable");
