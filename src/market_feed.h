// market_feed.h — replay de um feed de cotações através de SpscQueue.
//
// NÃO precisa ser editado: é o "cliente" da fila. A thread que chama
// replay_feed() faz o papel da thread de rede (produtora); uma thread criada
// aqui faz o papel da estratégia (consumidora) e chama `handler` para cada
// cotação, na ordem em que chegaram.
#ifndef MARKETFEED_MARKET_FEED_H
#define MARKETFEED_MARKET_FEED_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct Quote {
    std::uint64_t seq = 0;      // número de sequência do feed (estritamente crescente)
    std::string symbol;         // ex.: "PETR4.SA.LOTE-PADRAO" (maior que o SSO do std::string)
    std::int64_t price_ticks = 0;
    std::uint32_t quantity = 0;
};

struct FeedStats {
    std::uint64_t delivered = 0;        // cotações entregues ao handler
    std::uint64_t producer_stalls = 0;  // vezes em que a fila estava cheia para o produtor
};

// Entrega cada cotação de `quotes` a `handler` (na thread consumidora), em ordem,
// por uma SpscQueue<Quote> de capacidade `queue_capacity`. Retorna quando todas
// foram entregues.
FeedStats replay_feed(const std::vector<Quote>& quotes, std::size_t queue_capacity,
                      const std::function<void(const Quote&)>& handler);

#endif
