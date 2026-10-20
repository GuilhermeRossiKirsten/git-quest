// CONTRATO: Serializable aceita números, bool e texto (string, string_view, literal)
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

static_assert(gq::Serializable<int>, "int deve ser Serializable");
static_assert(gq::Serializable<double>, "double deve ser Serializable");
static_assert(gq::Serializable<bool>, "bool deve ser Serializable");
static_assert(gq::Serializable<std::string>, "std::string deve ser Serializable");
static_assert(gq::Serializable<std::string_view>, "std::string_view deve ser Serializable");
static_assert(gq::Serializable<char[6]>, "um literal de texto (char[N]) deve ser Serializable");
