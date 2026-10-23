// O consumidor pediu C++23 (CXX_STANDARD 23). O requisito da biblioteca é um
// MÍNIMO (C++20): ele não pode rebaixar o padrão escolhido por quem a usa.
#include <textstats/textstats.h>

static_assert(__cplusplus >= 202302L,
              "GQ_CXX23: o padrao do consumidor foi rebaixado por uma flag vinda da biblioteca");

int main() { return textstats::version()[0] == '1' ? 0 : 1; }
