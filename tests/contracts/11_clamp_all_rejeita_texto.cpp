// CONTRATO: clamp_all rejeita range de texto
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

static_assert(!probe::CanClampAll<std::vector<std::string>, std::string>,
              "clamp_all(vector<string>&, ...) NÃO deve compilar");
