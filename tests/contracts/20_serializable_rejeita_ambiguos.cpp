// CONTRATO: Serializable e serialize rejeitam char, containers e structs
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

static_assert(!gq::Serializable<char>, "char NÃO deve ser Serializable (número ou letra?)");
static_assert(!gq::Serializable<std::vector<int>>, "vector<int> NÃO deve ser Serializable");
static_assert(!gq::Serializable<Point>, "Point NÃO deve ser Serializable");
static_assert(!probe::CanSerialize<char>, "serialize('A') NÃO deve compilar");
static_assert(!probe::CanSerialize<std::vector<int>>, "serialize(vector<int>) NÃO deve compilar");
