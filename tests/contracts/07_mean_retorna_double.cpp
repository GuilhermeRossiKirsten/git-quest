// CONTRATO: mean retorna double para qualquer NumericRange
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

static_assert(std::same_as<decltype(gq::mean(std::declval<const std::vector<int>&>())), double>,
              "mean(vector<int>) deve retornar double");
static_assert(std::same_as<decltype(gq::mean(std::declval<const std::list<float>&>())), double>,
              "mean(list<float>) deve retornar double");
