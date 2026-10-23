// Código do CONSUMIDOR que gera warnings de propósito. Warnings são problema de
// quem escreveu este arquivo: as flags de warning/erro da biblioteca não devem
// vazar para cá e transformar isto em erro de compilação.
#include <textstats/textstats.h>

[[deprecated("use outra coisa")]] int gq_old_api() { return 1; }

int gq_noisy() {
    int gq_unused_in_consumer = 42;   // -Wunused-variable (só com -Wall)
    unsigned gq_u = 1;
    int gq_i = -1;
    const bool gq_cmp = gq_i < gq_u;  // -Wsign-compare (só com -Wall/-Wextra)
    return gq_old_api() + (gq_cmp ? 1 : 0);  // -Wdeprecated-declarations (ligado por padrão)
}
