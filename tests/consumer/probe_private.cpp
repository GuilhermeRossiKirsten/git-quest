// Sonda do avaliador: o consumidor da biblioteca instalada NÃO pode alcançar os
// headers internos (tokenizer.h, utf8.h, config.h). Se algum estiver no caminho
// de includes do consumidor, este arquivo deixa de compilar e diz qual vazou.
// (Não inclui a API pública de propósito: esta sonda não depende do padrão de C++.)
#if __has_include("tokenizer.h") || __has_include(<tokenizer.h>) || __has_include(<textstats/tokenizer.h>) || \
    __has_include(<textstats/detail/tokenizer.h>) || __has_include(<detail/tokenizer.h>)
#error "GQ_LEAK tokenizer.h"
#endif
#if __has_include("utf8.h") || __has_include(<utf8.h>) || __has_include(<utf8/utf8.h>) || \
    __has_include(<textstats/utf8.h>) || __has_include(<textstats/utf8/utf8.h>)
#error "GQ_LEAK utf8.h"
#endif
#if __has_include("config.h") || __has_include(<config.h>) || __has_include(<textstats/config.h>)
#error "GQ_LEAK config.h"
#endif

int main() { return 0; }
