// ingestor.h — serviço de ingestão: N workers consomem registros de uma fila
// bloqueante e entregam cada um a um handler; um Heartbeat informa
// periodicamente quantos registros ainda esperam na fila.
//
// Contrato público (mantenha):
//
//   Ingestor(std::size_t workers, std::chrono::milliseconds heartbeat_interval,
//            Handler handler, PendingObserver on_heartbeat = {})
//       workers == 0 ou handler vazio lançam std::invalid_argument; um intervalo
//       de heartbeat inválido também lança std::invalid_argument (vindo de
//       Heartbeat). Se o construtor lançar, nenhuma thread pode ficar para trás
//       e o processo continua de pé.
//
//   bool submit(Record record)
//       Enfileira um registro; devolve false depois de shutdown().
//       Pode ser chamado por várias threads ao mesmo tempo.
//
//   void shutdown()
//       Encerramento GRACIOSO: para de aceitar registros, processa TODOS os que
//       já foram aceitos (cada um exatamente uma vez), para o heartbeat e espera
//       todas as threads terminarem. Com a fila vazia, retorna em bem menos de
//       200 ms, mesmo com o heartbeat configurado para vários segundos.
//       Idempotente. Não chame de dentro do handler.
//
//   ~Ingestor()
//       Se shutdown() já foi chamado, apenas libera os recursos. Senão,
//       INTERROMPE: registros que ainda estão na fila são descartados (nunca
//       processados), cada worker termina no máximo o registro em andamento, e
//       o destrutor só retorna depois que todas as threads terminaram. Nenhum
//       handler roda depois que o destrutor retorna.
//
//   std::size_t processed() const
//       Quantos registros já passaram pelo handler. Pode ser lido de qualquer thread.
//
// O handler é chamado concorrentemente pelos workers (ele precisa ser thread-safe).
#ifndef GITQUEST_INGESTOR_H
#define GITQUEST_INGESTOR_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "blocking_queue.h"
#include "heartbeat.h"

struct Record {
    std::uint64_t id = 0;
    std::string body;
};

class Ingestor {
public:
    using Handler = std::function<void(const Record&)>;
    using PendingObserver = std::function<void(std::size_t pending)>;

    Ingestor(std::size_t workers, std::chrono::milliseconds heartbeat_interval, Handler handler,
             PendingObserver on_heartbeat = {});
    ~Ingestor();

    Ingestor(const Ingestor&) = delete;
    Ingestor& operator=(const Ingestor&) = delete;

    bool submit(Record record);
    void shutdown();
    std::size_t processed() const;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    Handler handler_;
    PendingObserver on_heartbeat_;
    BlockingQueue<Record> queue_;
    std::unique_ptr<Heartbeat> heartbeat_;
    bool running_ = true;
    bool stopped_ = false;
    std::size_t processed_ = 0;
};

#endif
