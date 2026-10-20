# Dicas — desafio-13

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Comece pelos sintomas mais baratos: rode `./test.sh` e leia primeiro a seção `[tsan]`.
Cada data race aponta DUAS linhas de `src/` — quem lê e quem escreve a mesma variável
sem sincronização. Depois pergunte, para cada thread que o projeto cria:

- quem **pede** para ela parar, e como ela **fica sabendo** se estiver dormindo ou
  bloqueada numa espera?
- quem **espera** ela terminar — e isso acontece em todos os caminhos, inclusive
  quando o construtor lança e quando ninguém chama `shutdown()`?

Rode `GQ_VERBOSE=1 ./test.sh` para ver o relatório completo de cada falha.

## Nível 2

Há quatro famílias de problema, uma em cada lugar:

- `BlockingQueue::pop` (`src/blocking_queue.h`): o que acontece se a thread acordar e
  a condição ainda não for verdadeira (despertar espúrio, ou outro consumidor pegou o
  item antes)? Quantos consumidores `close()` acorda? E quem acorda um consumidor
  bloqueado quando alguém pede parada no `stop_token` que ele recebeu?
- `Heartbeat` (`src/heartbeat.cpp`): `sleep_for` não pode ser interrompido. E o que
  `detach()` faz com uma thread que ainda usa `this` depois do destrutor?
- `Ingestor` (`src/ingestor.cpp`): `bool running_` lido por várias threads; o que um
  worker faz com os registros que ainda estão na fila quando vê `running_ == false`?
- Construtor de `Ingestor`: se `Heartbeat` lançar, o que os destrutores dos membros
  fazem com as threads que já foram criadas? O que o destrutor de um `std::thread`
  ainda *joinable* faz? (Veja o cap. 3 de *Asynchronous Programming with C++*, que
  apresenta `std::jthread` justamente como resposta ao `join()` esquecido.)

## Nível 3

A abordagem C++20:

- Troque `std::thread` por `std::jthread`. A função da thread pode receber um
  `std::stop_token` como primeiro parâmetro; o destrutor de `jthread` chama
  `request_stop()` e depois `join()`.
- Troque `std::condition_variable` por `std::condition_variable_any` e espere com a
  sobrecarga `wait(lock, stop_token, predicado)` (ou `wait_for(lock, stop_token, tempo,
  predicado)` no heartbeat, no lugar do `sleep_for`). Ela reavalia o predicado a cada
  despertar e acorda sozinha quando a parada é pedida — por dentro ela registra um
  `std::stop_callback`. A alternativa manual é você mesmo registrar um `std::stop_callback`
  que faz `notify_all()`.
- `close()` precisa acordar todo mundo.
- Contadores lidos de outras threads viram `std::atomic`; flags de parada viram stop tokens.
- Separe os dois encerramentos: `shutdown()` fecha a fila e deixa os workers esvaziarem
  o que foi aceito; o destrutor (sem `shutdown()`) pede parada a todos os workers e os
  workers verificam o token antes de pegar o próximo registro.

## Nível 4

Se ainda sobrar algum ✘:

- **Ordem dos membros.** Membros são destruídos na ordem inversa da declaração. Um
  `std::jthread` (ou `std::vector<std::jthread>`) declarado ANTES da fila, do mutex ou do
  callback que a thread usa é destruído DEPOIS deles: a thread ainda está rodando quando
  aquilo some. Declare as threads por último.
- **Pare todos antes de juntar.** `std::vector<std::jthread>` destrói um elemento por vez
  (pede parada e junta o primeiro, depois o segundo...). Para os workers pararem juntos,
  peça `request_stop()` a todos antes.
- **`shutdown()` lento** com fila vazia: algum `join()` está esperando uma thread que dorme
  ou espera sem olhar o token.
- **`processed()` diferente do aceito**: algum worker sai do laço com itens ainda na fila
  (no `shutdown()` a saída deve vir de `pop()` devolvendo `std::nullopt`, não de uma flag).
