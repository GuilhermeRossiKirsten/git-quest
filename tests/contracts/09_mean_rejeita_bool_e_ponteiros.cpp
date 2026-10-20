// CONTRATO: mean rejeita vector<bool> e vector<int*>
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

static_assert(!probe::CanMean<std::vector<bool>>, "mean(vector<bool>) NÃO deve compilar");
static_assert(!probe::CanMean<std::vector<int*>>, "mean(vector<int*>) NÃO deve compilar");
