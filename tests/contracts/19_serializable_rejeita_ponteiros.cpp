// CONTRATO: Serializable e serialize rejeitam ponteiros (inclusive const char*)
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

static_assert(!gq::Serializable<int*>, "int* NÃO deve ser Serializable");
static_assert(!gq::Serializable<const char*>, "const char* NÃO deve ser Serializable");
static_assert(!probe::CanSerialize<int*>, "serialize(int*) NÃO deve compilar");
static_assert(!probe::CanSerialize<const char*>, "serialize(const char*) NÃO deve compilar");
