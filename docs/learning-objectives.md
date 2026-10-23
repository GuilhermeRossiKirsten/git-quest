# Objetivos de aprendizagem — desafio-35

Ao concluir este desafio você será capaz de:

1. Explicar por que relógios físicos com desvio não servem para ordenar
   escritas causalmente relacionadas: uma réplica com o relógio adiantado
   "vence" escritas que aconteceram depois dela.
2. Implementar o relógio de Lamport, inclusive a regra de recebimento
   `max(local, remoto) + 1`, que garante C(envio) < C(recebimento).
3. Comparar relógios vetoriais distinguindo "aconteceu antes" de
   concorrência — uma ordem parcial, não a ordem lexicográfica.
4. Implementar entrega causal: reconhecer, pelo vetor da mensagem, se suas
   dependências já foram aplicadas, e reter a mensagem até lá.
5. Tornar a aplicação de mensagens idempotente numa rede "pelo menos uma vez".
6. Resolver escritas concorrentes com uma ordem total determinística
   (Lamport, ID) — e entender por que um desempate "par a par" que não é
   uma ordem total faz as réplicas divergirem.
7. Usar testes de propriedade com muitas sementes numa simulação
   determinística, e reproduzir uma falha a partir da semente.

## Como os testes verificam cada objetivo

| Objetivo | Verificação (`tests/`) |
|---|---|
| 1. Relógio físico não ordena causalidade | `TestCenario_RelogioAdiantado` (`scenario_test.go`): r0 tem o relógio adiantado 30 ms, 1,5 s ou 1 min; r1 lê o pedido "aberto" escrito por r0 e grava "cancelado"; em 30 combinações de desvio e semente, todas as réplicas precisam terminar com "cancelado". `TestPropriedade_Causalidade` também usa desvios de ±3 s. |
| 2. Lamport | `TestLamport` (`clock_test.go`): valores exatos de `Tick`/`Observe` (remoto maior, menor e igual ao local) e 2000 mensagens entre três processos que andam em ritmos diferentes, exigindo carimbo de recebimento maior que o de envio. |
| 3. Vetores | `TestVetor`: tabela com os quatro resultados (inclusive pares concorrentes que a comparação lexicográfica chama de "antes"), `Clone` sem compartilhar memória, `Merge`, e 200 histórias aleatórias de 3 a 5 processos em que `Compare` precisa coincidir com a alcançabilidade no grafo de eventos. |
| 4. Entrega causal | `TestCenario_PostAntesDaResposta`: com o enlace r0→r2 retido, a resposta de r1 chega a r2 antes da pergunta; r2 não pode mostrá-la sozinha e, quando a pergunta chega, precisa mostrar as duas. `TestPropriedade_Causalidade` (500 sementes): depois de **cada** entrega, nenhuma réplica mostra `reply:k` sem `post:k`; no fim, nada fica retido e nenhuma edição é desfeita pela versão que ela editou. |
| 5. Idempotência | `TestCenario_MensagemDuplicada` (toda mensagem chega duas vezes) e `TestPropriedade_Idempotencia` (500 sementes, 50% de duplicação): depois de cada entrega, nenhum contador passa do total já somado; no fim, todos iguais ao total. |
| 6. Ordem total nos conflitos | `TestCenario_EscritasConcorrentes`: empate de Lamport → vence o maior ID, mesmo que a outra escrita seja posterior no tempo real e no relógio físico; Lamport 6 × 1 → vence o 6, mesmo com relógio físico contrário (20 sementes cada). `TestPropriedade_Convergencia` (500 sementes): após a quiescência todas as réplicas mostram o mesmo estado e as somas certas; nas 25 primeiras sementes, repetir a semente dá o mesmo estado. |
| 7. Sementes e reprodução | Toda falha de propriedade informa a semente e o comando `GQ_SEED=<n> ./test.sh`, que roda só aquela semente com o diário completo (escritas, leituras e entregas) em `build/test.log`. |
| Transversal | `TestReplica_AcessoConcorrente` (clientes em goroutines enquanto a rede entrega), `-race` em toda a suíte e `go vet ./...` sem alertas. `TestReplica_CaminhoFeliz` confirma que o avaliador está de pé (já passa no código inicial). |
