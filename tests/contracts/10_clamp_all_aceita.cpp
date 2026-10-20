// CONTRATO: clamp_all aceita range numérico modificável com limites numéricos
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

static_assert(probe::CanClampAll<std::vector<double>, int>,
              "clamp_all(vector<double>&, 0, 1) deve compilar");
static_assert(probe::CanClampAll<std::list<int>, int>,
              "clamp_all(list<int>&, 0, 10) deve compilar");
