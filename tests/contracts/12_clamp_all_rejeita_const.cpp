// CONTRATO: clamp_all rejeita range que não pode ser modificado (const)
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

static_assert(!probe::CanClampAll<const std::vector<int>, int>,
              "clamp_all(const vector<int>&, ...) NÃO deve compilar: o range precisa ser modificável");
