# Objetivos de aprendizagem — desafio-22

Ao concluir este desafio você será capaz de:

1. Explicar por que TCP é um fluxo de bytes sem fronteiras de mensagem e
   enquadrar mensagens com prefixo de tamanho (*length-prefix framing*).
2. Ler corretamente de um `io.Reader`/`net.Conn`: tratar leituras parciais com
   `io.ReadFull` e distinguir o fim limpo do fluxo (`io.EOF`) do fim no meio
   de uma mensagem (`io.ErrUnexpectedEOF`).
3. Validar o que chega da rede antes de alocar memória: um limite de tamanho
   de frame impede que 4 bytes maliciosos custem gigabytes.
4. Definir um formato no fio sem ambiguidade — largura e ordem dos bytes do
   cabeçalho — que um cliente em qualquer linguagem consiga implementar.
5. Usar os deadlines de `net.Conn` (instantes absolutos) para implementar
   *idle timeout* por frame e *write timeout* por resposta, derrubando
   clientes mudos, *slowloris* e consumidores lentos sem punir clientes ativos.
6. Aplicar backpressure: fila de saída limitada, um único escritor por
   conexão, e o próprio TCP freando o cliente que não lê.
7. Encerrar um servidor TCP sem vazar goroutines: fechar o listener e as
   conexões abertas e esperar os handlers.

## Como os testes verificam cada objetivo

| Objetivo | Verificação (`tests/`) |
|---|---|
| 1. Framing sobre TCP | `TestServer_EcoBasico` (caminho feliz) e `TestServer_FragmentacaoTCP` (`server_test.go`): 160 frames de 0 a 1 MiB passam por um proxy TCP que entrega os bytes em pedaços de 1 a 4093 bytes, com pausas, nos dois sentidos; os ecos precisam chegar íntegros e em ordem. |
| 2. Leituras parciais e EOF | `TestFrame_LeiturasParciais` (`frame_test.go`): o mesmo fluxo de 9 frames lido com `iotest.OneByteReader`, `iotest.HalfReader`, `iotest.DataErrReader` e um leitor de pedaços aleatórios; depois do último frame, `io.EOF`. `TestFrame_EOF`: 7 cenários (fluxo vazio, frame vazio, cabeçalho cortado, corpo ausente, corpo cortado…), cada um também com 1 byte por `Read`, exigindo `io.EOF` ou `io.ErrUnexpectedEOF` conforme o caso. |
| 3. Limite antes de alocar | `TestFrame_TamanhoMaximo`: num processo filho (para que uma alocação de 4 GiB não derrube a suíte), cabeçalhos `0xFFFFFFFF`, `0x7FFFFFFF` e `MaxSize+1` devem dar `ErrTooLarge` com menos de 64 KiB alocados (`runtime.MemStats.TotalAlloc`); frame acima de `MaxSize` com corpo presente é recusado; frame de exatamente `MaxSize` é aceito; `WriteFrame` recusa payload grande sem escrever nada. |
| 4. Formato no fio | `TestFrame_FormatoNoFio`: para tamanhos de 0 a 1 MiB (incluindo 65 535, 65 536 e 70 000), os bytes produzidos por `WriteFrame` precisam ser idênticos aos do codificador de referência (`uint32` big-endian + payload) e voltar intactos por `ReadFrame`. |
| 5. Deadlines | `TestServer_IdleTimeout` (`IdleTimeout` = 400 ms): conexão muda cai entre 300 ms e 4 s; *slowloris* (1 byte a cada 100 ms, frame que levaria 4,4 s) cai em menos de 3 s; cliente que manda um frame a cada 100 ms por 1,5 s recebe todos os ecos. `TestServer_ConsumidorLento` (`WriteTimeout` = 300 ms): cliente que envia sem ler é desconectado — a conexão não pode continuar aberta por 5 s. |
| 6. Backpressure | `TestServer_Backpressure`: com buffers de socket de ~64 KiB, um cliente que envia frames sem ler nenhuma resposta precisa ser contido (escrita bloqueada por 1 s ou conexão derrubada) antes de 4 MiB aceitos, e outro cliente continua recebendo ecos. `TestServer_PipelineEmOrdem`: 3000 frames em pipeline voltam íntegros e na mesma ordem (um único escritor por conexão). |
| 7. Encerramento sem vazamento | `TestServer_CloseEncerraTudo`: com uma conexão ociosa, uma no meio de um frame e uma que não lê, `Close` retorna em até 3 s, `Serve` devolve `ErrServerClosed`, as três conexões são fechadas, novas conexões são recusadas e nenhuma goroutine nova de `src/echo` sobrevive (dump de goroutines com 2 s de tolerância). |
| Transversal | `go vet ./...` sem alertas e `-race` em toda a suíte. |
