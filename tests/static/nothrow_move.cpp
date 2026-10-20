// nothrow_move.cpp — verificação em tempo de compilação (não gera executável).
//
// tests/run.sh compila este arquivo isoladamente com -fsyntax-only: se algum
// static_assert falhar, só este critério é reprovado.
#include <type_traits>

#include "../../src/batch.h"

static_assert(std::is_nothrow_move_constructible<Buffer>::value,
              "Buffer: o construtor de movimento precisa ser noexcept");
static_assert(std::is_nothrow_move_assignable<Buffer>::value,
              "Buffer: a atribuição por movimento precisa ser noexcept");
static_assert(std::is_nothrow_move_constructible<Message>::value,
              "Message: o construtor de movimento precisa ser noexcept (existe um?)");
static_assert(std::is_nothrow_move_assignable<Message>::value,
              "Message: a atribuição por movimento precisa ser noexcept (existe uma?)");
static_assert(std::is_copy_constructible<Message>::value && std::is_copy_assignable<Message>::value,
              "Message: precisa continuar copiável (semântica de valor)");
