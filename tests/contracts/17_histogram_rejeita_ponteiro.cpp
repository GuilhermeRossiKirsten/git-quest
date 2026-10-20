// CONTRATO: Histogram rejeita chave ponteiro (contaria endereços, não conteúdo)
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

static_assert(!probe::CanMakeHistogram<const char*>, "Histogram<const char*> NÃO deve compilar");
static_assert(!probe::CanMakeHistogram<int*>, "Histogram<int*> NÃO deve compilar");
