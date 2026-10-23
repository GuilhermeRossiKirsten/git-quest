# Dicas — desafio-34

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Comece pelo cliente que você não pode mudar: leia `tests/v1client_test.go` e
liste cada campo que o ERP lê e cada campo que ele envia. Compare com o que
`src/api` produz e consome hoje. Renomear um campo é uma mudança aditiva?

Depois, para cada sintoma do README, pergunte:

- Paginação: o `next` aponta para "a posição 3" ou para "depois do produto
  X"? O que acontece com "a posição 3" quando três produtos entram na frente?
- Edição concorrente: como o servidor saberia que o Beto editou uma versão
  velha? O que o cliente teria de mandar junto?
- Pedido reenviado: o que o servidor precisaria lembrar da primeira vez para
  responder igual na segunda?
- Erros: um programa que recebe `500 text/plain` consegue distinguir "produto
  não existe" de "servidor caiu"? E de "você mandou JSON quebrado"?

## Nível 2

Lugares para olhar:

- `productJSON` e `orderInput`/`orderJSON`: o PR #412 trocou nomes. Campos
  podem conviver — `price` (reais) **e** `price_cents`; `quantity` **e** `qty`.
- `listProducts`: `offset` é uma posição num conjunto que muda. O store dá a
  cada produto um `Seq` crescente que nunca muda — um bom ponto de referência.
  Ah, e `limit` negativo faz o quê com aquele `all[offset:end]`?
- `internal/store`: compare `ReplaceProduct` com `ReplaceProductIfVersion`.
  "Ler, comparar e depois gravar" em duas chamadas não é atômico — os testes
  intercalam outra escrita exatamente nesse buraco.
- `ETag` e `If-None-Match`/`If-Match` (RFC 9110, seções 8.8.3 e 13.1): qual
  dado do produto muda a cada gravação? Quem mais altera um produto além do
  `PUT`?
- `writeError` responde tudo igual. Faça um mapa erro → status (o README tem a
  tabela) e uma única função que escreve `application/problem+json`.
- `Idempotency-Key`: o servidor precisa guardar, por chave, uma impressão
  digital da requisição e a resposta. E se a segunda cópia chegar **enquanto**
  a primeira ainda está gravando?

## Nível 3

Uma abordagem possível, em passos pequenos (rode `./test.sh` entre eles):

1. **Compatibilidade**: devolva `price` (centavos ÷ 100) ao lado de
   `price_cents`, e `quantity` + `total` ao lado de `qty` + `total_cents`; na
   entrada, aceite `quantity` (e `qty`). Nada de remover campos.
2. **Erros**: crie `writeProblem(w, status, tipo, título, detalhe)` que grava
   `Content-Type: application/problem+json` e o JSON com `type`, `title`,
   `status`, `detail`. Use `errors.Is` com os erros do store para escolher
   404/409/422; JSON inválido é 400; validação é 422. O `detail` é texto seu,
   nunca `err.Error()` do store ou do `encoding/json`.
3. **Paginação**: valide `limit` (1..100, padrão 20). O cursor codifica o `Seq`
   do último item entregue (por exemplo, base64 de `v1:<seq>`); a próxima
   página é "os itens com `Seq` menor que o do cursor". Cursor que não
   decodifica → 400. Sem mais itens → `next: null`.
4. **ETag**: derive um valor opaco do ID + `Version` do produto, entre aspas.
   Envie-o em GET, POST e PUT. No GET, se `If-None-Match` casar (lista, `*`,
   `W/` ignorado) → 304 sem corpo, repetindo o `ETag`.
5. **If-Match**: sem cabeçalho → 428. Leia o produto, compare o ETag (forte);
   se não casar → 412. Grave com `ReplaceProductIfVersion(…, versão lida)` e
   trate `ErrVersionMismatch` como 412.
6. **Idempotência**: um mapa protegido por mutex, chave → {hash do corpo,
   pronto?, status, cabeçalhos, corpo}. Reserve a chave (pronto = falso)
   **antes** de criar o pedido; quem chegar com a chave reservada e não pronta
   recebe 409; com hash diferente, 422; pronta, o replay. Para capturar a
   resposta, `httptest.NewRecorder()` funciona bem.

## Nível 4

Checklist final, item a item:

- `If-None-Match` usa comparação **fraca** (`W/"x"` casa com `"x"`); `If-Match`
  usa comparação **forte**.
- O ETag precisa mudar quando um pedido baixa o estoque: se ele vier só de
  nome e preço, o app vê 304 com o estoque velho.
- O cursor não pode depender de o último item ainda existir: se ele for
  removido, a travessia continua.
- Guarde no cache de idempotência a resposta inteira (status, `Location`,
  corpo) — o replay tem de ser idêntico. Respostas 5xx não devem ser
  guardadas (o cliente pode tentar de novo).
- O 304 não tem corpo; o 412/428 não alteram nada; uma requisição inválida
  nunca baixa estoque.
