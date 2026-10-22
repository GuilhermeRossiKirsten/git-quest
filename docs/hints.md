# Dicas — desafio-22

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Releia a documentação de `io.Reader` (`go doc io.Reader`) e responda: se você
pede 4 bytes a um `Read`, quantos bytes ele pode devolver? E se pede 70 000? O
que significa um `Read` que devolve dados **e** `io.EOF` na mesma chamada?

Depois pense no TCP como um cano de bytes: o remetente fez dois `Write`, mas
nada obriga o destinatário a ver duas leituras — nem as mesmas fronteiras.
Onde, em `src/frame/frame.go`, o código assume o contrário?

Para o servidor, desenhe a vida de uma conexão: quem lê, quem escreve, quantas
goroutines existem para ela, o que acontece se o cliente parar de ler, e o que
acontece com tudo isso quando `Close` é chamado.

## Nível 2

Lugares para olhar:

- `ReadFrame` chama `r.Read` uma vez para o cabeçalho e uma vez para o corpo.
  `go doc io.ReadFull` — e repare no que ele devolve quando **nenhum** byte foi
  lido versus quando parte foi lida. Um frame cujo cabeçalho chegou inteiro e
  o corpo não chegou é um fim "limpo"?
- Quanto `ReadFrame` aloca antes de saber se o cabeçalho é razoável? Quem
  escolhe esse número?
- Escreva um frame de 70 000 bytes num `bytes.Buffer` e olhe os 4 primeiros
  bytes. Compare com `binary.BigEndian.PutUint32`.
- `SetDeadline` recebe um **instante**, não uma duração. Chamado uma vez, no
  `Accept`, ele vale para a conexão inteira — e para leituras **e** escritas.
- `go func() { ... WriteFrame(conn, msg) }()` por mensagem: em que ordem as
  respostas saem? Quantas goroutines existem se o cliente não lê? O que
  acontece quando duas delas escrevem cabeçalho e corpo ao mesmo tempo?
- `Close` fecha o listener. Quem fecha as conexões já aceitas? Quem espera
  pelos handlers?

## Nível 3

Uma abordagem possível, em passos pequenos (rode `./test.sh` entre eles):

1. `ReadFrame`: `io.ReadFull` no cabeçalho (o erro dele já distingue `io.EOF`
   de `io.ErrUnexpectedEOF`); valide o tamanho contra `MaxSize` **antes** de
   alocar; `io.ReadFull` no corpo, convertendo `io.EOF` em
   `io.ErrUnexpectedEOF`. Embrulhe `ErrTooLarge` com `%w` se quiser contexto.
2. `WriteFrame`: cabeçalho com `binary.BigEndian.PutUint32`; de preferência
   cabeçalho e corpo num único buffer e num único `Write`.
3. No servidor, um **único escritor** por conexão, alimentado por um canal
   **com buffer limitado**. O leitor envia para o canal e, se ele estiver
   cheio, bloqueia — e, parando de ler, deixa o TCP frear o cliente. Use um
   `select` para o leitor não ficar preso se o escritor morrer.
4. Prazos: antes de **cada frame**, `SetReadDeadline(time.Now().Add(idle))`
   (o frame inteiro precisa chegar nesse prazo); antes de **cada escrita**,
   `SetWriteDeadline(time.Now().Add(writeTimeout))`. Se a escrita falhar,
   feche a conexão.
5. Em `Server`, guarde as conexões abertas num mapa protegido por mutex e use
   um `sync.WaitGroup` para os handlers. `Close`: marca fechado, fecha o
   listener e todas as conexões, e espera o `WaitGroup`.

## Nível 4

Checklist final, item a item:

- O prazo de leitura é renovado só quando um frame **completa**. Renovar a
  cada `Read` (por exemplo, num `io.Reader` que embrulha a conexão) deixa o
  slowloris viver para sempre.
- Fila sem limite (slice que só cresce, goroutine por mensagem) não é
  backpressure: só muda o lugar onde a memória acaba.
- O `wg.Add` de uma conexão nova precisa acontecer sob o mesmo mutex que
  `Close` usa para marcar "fechado" — senão uma conexão aceita no último
  instante escapa do `Wait`.
- Quando o leitor termina (EOF do cliente), feche o canal e espere o escritor
  entregar o que ainda está na fila antes de fechar a conexão.
- `net.ErrClosed` e `io.EOF` são o fim normal de uma conexão: não precisam de
  log de aviso.
