# Objetivos de aprendizagem — desafio-34

Ao concluir este desafio você será capaz de:

1. Evoluir uma API sem quebrar clientes que não podem ser atualizados:
   distinguir mudanças aditivas (compatíveis) de renomeações e trocas de tipo
   (incompatíveis), e manter campos antigos ao lado dos novos.
2. Paginar coleções que mudam com um cursor opaco e um link `next`,
   explicando por que paginação por offset repete e pula itens sob inserções
   e remoções.
3. Proteger escritas contra *lost update* com ETag forte, `If-Match`, 412 e
   428, apoiando a verificação numa gravação condicional atômica.
4. Tornar um `POST` seguro para retentativas com `Idempotency-Key`: replay da
   mesma resposta, rejeição de chave reutilizada com outro corpo e reserva da
   chave contra cópias simultâneas.
5. Reportar erros com Problem Details (RFC 9457) e o código HTTP certo
   (400/404/409/412/422/428), sem expor detalhes internos.
6. Implementar GET condicional com `If-None-Match` e 304, com comparação
   fraca de ETags.

## Como os testes verificam cada objetivo

| Objetivo | Verificação (`tests/`) |
|---|---|
| 1. Compatibilidade | `TestCompat_ClienteV1` (`v1client_test.go`): o ERP do parceiro — código congelado, leitor tolerante — sincroniza 7 produtos seguindo `next` e exige `price` em reais igual a `price_cents/100`; faz um pedido com `quantity` e exige `quantity` e `total` na resposta. O mesmo teste exige que `price_cents` e `total_cents` continuem presentes para o app novo; `TestCaminhoFeliz` garante que o fluxo do app novo não regrediu. |
| 2. Paginação estável | `TestPaginacao_Cursor`: 10 produtos, `limit=3`; depois da 1ª página entram 3 produtos novos; o mesmo `next` é pedido duas vezes (mesma página); depois um produto já visto e o produto apontado pelo cursor são removidos. A travessia tem de ser exatamente os 10 originais, na ordem, sem repetições; `next` não pode ser por `offset`; com tudo numa página, `next` é `null`. |
| 3. Lost update | `TestConcorrencia_IfMatch`: ETag forte e bem formado em POST/GET/PUT; `PUT` sem `If-Match` → 428 sem alterar nada; cenário Ana/Beto sequencial (412 para o ETag velho, sucesso depois de reler). Depois, intercalação determinística: um gancho do store pausa o 1º `PUT` **dentro** da gravação, o 2º `PUT` com o mesmo `If-Match` corre no meio; exige-se uma 200 e uma 412, e o estado final do vencedor. |
| 4. Idempotência | `TestIdempotencia_Pedidos`: replay com mesmo status, corpo e `Location`, sem novo pedido nem nova baixa de estoque; mesma chave com outro corpo → 422 problem+json; chave nova → pedido novo; sem chave → um pedido por POST; duas cópias simultâneas (a 1ª pausada dentro do `CreateOrder`) → um único pedido, a 2ª recebe 409 ou o mesmo 201. `TestCompat_RetentativaV1`: o transporte "perde" a resposta do 1º POST do ERP, que reenvia com a mesma chave → exatamente 1 pedido, o mesmo ID, estoque baixado uma vez. |
| 5. Problem Details | `TestErros_ProblemJSON`: 18 cenários (inexistentes, `limit`/`cursor` inválidos, JSON malformado, tipo errado, validação, produto desconhecido, estoque, `If-Match` ausente/velho) exigem o status certo, `Content-Type: application/problem+json`, `type` (URI), `title`, `status` igual ao HTTP e `detail` sem `store:`/`json:`/`strconv.`; e que nenhum deles altere o estado. |
| 6. GET condicional | `TestCondicional_IfNoneMatch`: ETag atual, lista com o ETag, `W/` + ETag e `*` → 304 sem corpo repetindo o ETag; ETag diferente → 200; depois de um pedido baixar o estoque, o ETag antigo → 200 com o estoque novo; depois de um `PUT`, o ETag devolvido pelo `PUT` → 304. |
| Transversal | `go vet ./...` sem alertas e `-race` em toda a suíte. |
