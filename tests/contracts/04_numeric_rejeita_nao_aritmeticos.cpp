// CONTRATO: Numeric rejeita tipos não aritméticos (string, ponteiro, complex, struct)
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

static_assert(!gq::Numeric<std::string>, "std::string NÃO deve ser Numeric");
static_assert(!gq::Numeric<int*>, "int* NÃO deve ser Numeric");
static_assert(!gq::Numeric<std::complex<double>>, "std::complex<double> NÃO deve ser Numeric");
static_assert(!gq::Numeric<Point>, "uma struct qualquer NÃO deve ser Numeric");
