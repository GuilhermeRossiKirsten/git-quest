# Objetivos de aprendizagem — desafio-36

Ao concluir este desafio você será capaz de:

1. Reconhecer um modelo de domínio anêmico (estado público, regras espalhadas
   pelos serviços e verificadas só em alguns caminhos) e mover as invariantes
   para a raiz do agregado, que passa a ser a única porta de mudança do pedido
   e de seus itens.
2. Encapsular estado em Go — campos não exportados, cópias defensivas de
   slices — e provar o encapsulamento com testes "não deve compilar".
3. Modelar dinheiro como value object imutável: centavos inteiros, moeda
   explícita, igualdade por valor, operações que devolvem valores novos e
   arredondamento determinístico de percentuais.
4. Calcular valores derivados (subtotal, desconto, total) a partir do estado
   atual em vez de mantê-los em caches que podem ficar desatualizados.
5. Registrar eventos de domínio somente para comandos aceitos, exatamente um
   por mudança de estado, entregá-los uma única vez e publicá-los só depois que
   a transação foi gravada.
6. Usar testes de propriedade com semente fixa e um modelo de referência para
   verificar que nenhuma sequência de comandos viola as invariantes.

## Como os testes verificam cada objetivo

| Objetivo | Verificação (`tests/`) |
|---|---|
| 1. Invariantes no agregado | `TestInvariantes_Itens` e `TestInvariantes_CicloDeVida` (`invariants_test.go`): tabela com ~30 comandos inválidos chamados **direto no agregado** (quantidade 0/negativa/10001, SKU vazio ou repetido, preço zero/negativo/em USD, crédito estourado em `AddLine` e `ChangeQuantity`, item em pedido confirmado/cancelado, confirmar vazio ou duas vezes, enviar rascunho/cancelado, cancelar enviado ou cancelado, cupom repetido/0%/101%, `NewOrder` com argumentos inválidos). Cada caso exige `errors.Is` com o erro sentinela, estado observável idêntico e nenhum evento; também confere as transições permitidas e que o crédito considera o desconto. |
| 2. Encapsulamento | `TestEncapsulamento_NaoCompila` (`encapsulation_test.go`): compila um programa-sonda de controle (só API pública; tem de compilar) e 8 sondas que tentam `o.State = ...`, `o.Items = nil`, `o.TotalAmount = m`, `o.Events = nil`, `o.Lines()[0].Qty = 0`, `m.Amount = 0.3`, `money.Money{Amount: ...}` e `ordering.Order{State: ...}`; cada uma tem de falhar **na linha da sonda**. `TestEncapsulamento_EstadoNaoVaza`: por reflexão, `Order`, `OrderLine` e `Money` sem campos exportados; alterar o slice de `Lines()` ou de `PullEvents()` não altera o pedido. |
| 3. Money como value object | `TestMoney_Centavos` (`money_test.go`): `New` preserva centavos (29 → 29), `0.1 + 0.2 == 0.3` por `Equal` e `==`, dez vezes 0.10 = 1.00, `Parse` exato e rejeitando `1.005`/`1e3`/`NaN`/`12,34`, moeda ISO de 3 letras maiúsculas, `ErrCurrencyMismatch` em `Add`/`Sub`, tabela de `Percent` com meio centavo (12,5 → 13; −12,5 → −13), `String`. `TestMoney_ValorImutavel`: `Add`/`Sub`/`Multiply`/`Percent` não alteram operandos, cópias independentes, chave de map, métodos no conjunto de métodos do valor (receptor por valor). |
| 4. Valores derivados | `TestTotal_SempreConsistente`: sequência incluir → mudar quantidade → cupom → mudar quantidade → confirmar, conferindo subtotal/desconto/total exatos a cada passo (inclusive o desconto recalculado sobre o subtotal novo e não reaplicado na confirmação) e `OrderConfirmed.Total`. |
| 5. Eventos de domínio | `TestEventos_ExatamenteUmaVez` (`events_test.go`): compara a lista exata de eventos (tipo e dados) depois de cada comando aceito, lista vazia depois de cada rejeitado e no segundo `PullEvents()`. `TestServico_PublicaSoAposSalvar`: repositório e publicador falsos; exige que cada publicação aconteça depois do `Save` da mesma operação, nada publicado quando o `Save` falha ou o domínio rejeita, nenhum evento republicado, e erros do domínio/repositório reconhecíveis por `errors.Is`. |
| 6. Propriedade | `TestPropriedade_SequenciasAleatorias` (`property_test.go`): `math/rand/v2` com PCG de semente fixa (36), 400 sequências × 40 comandos válidos e inválidos; depois de cada comando compara aceitação, evento e todo o estado observável com um modelo de referência em inteiros, e confere que o total nunca passa do limite. |
| Transversal | `go vet ./...` sem alertas. |
