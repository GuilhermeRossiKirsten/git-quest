# Dicas — desafio-35

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Desenhe a história do pedido cancelado: uma linha do tempo por réplica, uma
seta por mensagem. Anote em cada escrita o carimbo que o código usa para
decidir quem vence. A escrita "cancelado" aconteceu *depois* de "aberto" em
que sentido — no relógio de alguém, ou porque quem cancelou **leu** o
"aberto" antes? Qual desses dois sentidos o código está usando?

Depois, faça a mesma pergunta para a resposta sem pergunta: no momento em que
uma mensagem chega, o que a réplica sabe sobre o que o remetente já tinha
visto? E o que ela faz com essa informação?

E para as curtidas: a rede promete entregar cada mensagem *pelo menos* uma
vez. O que muda se for "pelo menos" em vez de "exatamente"?

## Nível 2

Lugares para olhar:

- `src/clock/lamport.go`: o que `Observe` faz com o carimbo recebido? Releia a
  regra de recebimento do relógio de Lamport (Tanenbaum, seção 6.2.1).
- `src/clock/vector.go`: `Compare` trata vetores como números de versão.
  `[1 0 0]` e `[0 1 0]` — qual aconteceu antes? Em quais pares *nenhum*
  aconteceu antes do outro?
- `src/kv/replica.go`, `apply`: qual carimbo decide o vencedor, e o que
  acontece num empate? Em empate, quem vence depende de quê?
- `Receive` aplica a mensagem assim que ela chega. O vetor `VC` que viaja na
  mensagem diz exatamente quantas escritas de cada réplica a origem tinha
  visto. O que a réplica local precisa ter aplicado antes?
- Rode `GQ_SEED=<semente que falhou> ./test.sh` e leia o diário: procure a
  entrega logo antes da violação.

## Nível 3

A abordagem (Tanenbaum, 6.2.2, "imposição de comunicação causalmente
ordenada"):

1. Conserte os relógios: `Observe` = máximo dos dois mais um; `Compare`
   precisa olhar **todas** as posições — se houver posições maiores dos dois
   lados, é `Concurrent`.
2. Cada réplica mantém um vetor que conta quantas escritas de cada origem já
   aplicou. A escrita local incrementa a própria posição e vai carimbada com
   uma cópia do vetor.
3. Ao receber uma escrita da origem `j`: se o vetor da mensagem na posição
   `j` não é maior que o seu, ela já foi aplicada — descarte (idempotência).
   Ela só pode ser entregue se for exatamente a próxima de `j` e se, em todas
   as outras posições, a mensagem não exigir mais do que você já tem. Senão,
   guarde num buffer.
4. Sempre que entregar algo, percorra o buffer de novo: uma entrega pode
   destravar outras. Ao entregar, atualize o vetor e o relógio de Lamport.
5. Para decidir o valor de uma chave, guarde junto dele a versão
   (Lamport, origem) e só troque por uma versão maior nessa ordem total. O
   relógio físico pode continuar na mensagem, mas não decide nada.

## Nível 4

Por que não "se for depois pelo vetor, vence; se for concorrente, vence o
maior ID"? Pense em três escritas na mesma chave: `a` de r2, `b` de r0 que
viu `a`, e `c` de r1 concorrente com as duas. Aplique a regra nas ordens de
chegada `a, c, b` e `a, b, c` e compare o resultado. Uma regra de conflito só
garante convergência se for uma **ordem total** compatível com a
causalidade — e (Lamport, ID) é uma (é o mesmo truque do multicast
totalmente ordenado da seção 6.2.1). Como a entrega causal garante que o
relógio de Lamport de quem escreve já viu tudo de que a escrita depende, a
escrita causalmente posterior sempre tem o carimbo maior.
