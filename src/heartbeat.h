// heartbeat.h — chama um callback periodicamente numa thread de fundo.
//
// Contrato público (mantenha):
//
//   Heartbeat(std::chrono::milliseconds interval, std::function<void()> beat)
//       Valida os argumentos ANTES de criar qualquer thread: interval <= 0 ou
//       beat vazio lançam std::invalid_argument. Depois inicia a thread de fundo,
//       que chama beat() a cada `interval` (a primeira chamada acontece depois
//       do primeiro intervalo, não imediatamente).
//
//   void stop()
//       Para o heartbeat e espera a thread terminar. Retorna rapidamente
//       (bem antes de 200 ms), mesmo que o intervalo seja de vários segundos.
//       Depois de stop(), beat() nunca mais é chamado. Idempotente.
//       Não chame stop() de dentro do próprio beat().
//
//   std::uint64_t beats() const
//       Quantas vezes beat() já foi chamado. Pode ser lido de qualquer thread.
//
//   ~Heartbeat()
//       Equivale a stop(): quando o destrutor retorna, a thread de fundo já
//       terminou e beat() não será mais chamado.
#ifndef GITQUEST_HEARTBEAT_H
#define GITQUEST_HEARTBEAT_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

class Heartbeat {
public:
    Heartbeat(std::chrono::milliseconds interval, std::function<void()> beat);
    ~Heartbeat();

    Heartbeat(const Heartbeat&) = delete;
    Heartbeat& operator=(const Heartbeat&) = delete;

    void stop();
    std::uint64_t beats() const;

private:
    void run();

    std::thread thread_;
    std::chrono::milliseconds interval_;
    std::function<void()> beat_;
    bool running_ = true;
    std::uint64_t beats_ = 0;
};

#endif
