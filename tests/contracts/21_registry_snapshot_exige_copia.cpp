// CONTRATO: Registry<T>::snapshot só existe se T é copiável; add/find continuam disponíveis
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

static_assert(!probe::CanSnapshot<gq::Registry<std::unique_ptr<int>>>,
              "snapshot() em Registry<unique_ptr<int>> NÃO deve compilar");
static_assert(probe::CanAddAndFind<std::unique_ptr<int>>,
              "add/find em Registry<unique_ptr<int>> devem continuar compilando");
