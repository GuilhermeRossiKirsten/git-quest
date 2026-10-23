#pragma once
// Conta as chamadas ao operator new global enquanto um AllocCounter existe.
// As substituições (alloc_counter.cpp) usam malloc/free, que o ASan continua
// interceptando: vazamentos, double free e uso após liberação seguem detectados.

namespace gq {

class AllocCounter {
 public:
  AllocCounter();
  ~AllocCounter();
  AllocCounter(const AllocCounter&) = delete;
  AllocCounter& operator=(const AllocCounter&) = delete;

  // Alocações desde a construção (ou desde o último reset()).
  long count() const;
  void reset();
};

}  // namespace gq
