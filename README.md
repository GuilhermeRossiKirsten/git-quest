# desafio-34 · A API que quebrava os clientes

> Trilha: architecture · Nível: intermediate · ~60 min · 200 pontos · Go 1.25.3 (`golang:1.25.3-bookworm`), só a biblioteca padrão

## Contexto

A loja de ferramentas vende por dois canais que conversam com a mesma API
HTTP JSON (`src/api`): o **app novo**, mantido pelo time interno, e o **ERP do
distribuidor parceiro** — um "cliente v1" escrito por uma consultoria em 2023.
A consultoria acabou, o código do ERP não muda mais, e ele fatura 40% das
vendas.

Na semana passada entrou em produção o PR #412, "dinheiro em centavos, chega
de float". O app novo foi atualizado junto e ficou ótimo. Desde então, o
parceiro abriu um chamado por dia — e o time do app, que parecia feliz, abriu
outros tantos.

## Problema

Sintomas relatados (rode `./start.sh`: a demonstração em `cmd/demo` reproduz
cada um e mostra *esperado* × *obtido*):

- O ERP recusa o catálogo inteiro: "produto sem preço". Os pedidos que ele
  manda voltam com **500**.
- Durante a sincronização noturna do ERP, enquanto a loja cadastra produtos,
  alguns produtos aparecem **duas vezes** e outros **nunca chegam**.
- A Ana baixou o preço da cadeira; o Beto, no mesmo minuto, corrigiu o
  estoque. A mudança da Ana **sumiu** — sem erro para ninguém.
- Um timeout no balanceador fez o ERP reenviar um pedido (com a mesma
  `Idempotency-Key`, como o contrato sempre permitiu): o cliente foi **cobrado
  duas vezes**.
- Qualquer erro — produto inexistente, JSON quebrado, estoque insuficiente —
  vira `500 text/plain` com mensagens como `store: not found` ou
  `json: cannot unmarshal…`. Um `?limit=-1` derruba a conexão.
- O app baixa o produto inteiro toda vez que a tela abre, mesmo sem mudança:
  não há ETag para perguntar "mudou?".

Seu trabalho é fazer a API cumprir o contrato abaixo — investigando e
corrigindo `src/api`, sem reescrever o serviço e **sem quebrar nenhum dos dois
clientes**.

### Contrato da API

Recursos e operações (respostas de sucesso em `application/json`; **todo**
erro em `application/problem+json`):

| Operação | Sucesso | Erros |
|---|---|---|
| `GET /products?limit=N[&cursor=C]` | 200, página | 400 (`limit` fora de 1..100 ou não numérico; `cursor` inválido) |
| `POST /products` | 201 + `Location` + `ETag` | 400 (JSON inválido), 422 (validação) |
| `GET /products/{id}` | 200 + `ETag`; **304** se `If-None-Match` casar | 404 |
| `PUT /products/{id}` (exige `If-Match`) | 200 + novo `ETag` | 400, 404, **412** (ETag não confere), 422, **428** (sem `If-Match`) |
| `DELETE /products/{id}` | 204 | 404 |
| `POST /orders` (aceita `Idempotency-Key`) | 201 + `Location` | 400, 409 (estoque insuficiente; ou mesma chave ainda em andamento), 422 (validação, produto desconhecido, mesma chave com outro corpo) |
| `GET /orders/{id}` | 200 | 404 |

Representações — **mudanças só aditivas**: cada campo do v1 continua lá, com
o mesmo nome, tipo e significado; os campos novos vêm ao lado.

| Recurso | Campo | Tipo | Quem usa |
|---|---|---|---|
| produto | `id`, `name`, `stock` | string, string, inteiro | todos |
| produto | `price` | número, **reais** (ex.: `19.9`) | cliente v1 |
| produto | `price_cents` | inteiro, centavos | app novo (é o que POST/PUT recebem) |
| página | `items` | lista de produtos, do mais novo para o mais antigo | todos |
| página | `next` | string (caminho) ou `null` na última página | todos — o cliente só **segue** o link, nunca o monta |
| pedido (entrada) | `product_id`, `quantity` | string, inteiro > 0 | todos (`qty` também é aceito, pelo app novo) |
| pedido (saída) | `id`, `product_id`, `quantity`, `status` (`"confirmed"`) | | cliente v1 |
| pedido (saída) | `total` (reais) e `total_cents` (centavos) | número, inteiro | v1 e app novo |

Regras de comportamento:

- **Paginação**: percorrer o catálogo seguindo `next` entrega cada produto que
  existiu do início ao fim da travessia **exatamente uma vez**, na ordem, mesmo
  com produtos sendo criados e removidos no meio. O mesmo `next` devolve a
  mesma página. `limit` padrão 20, máximo 100.
- **ETag**: forte e entre aspas (`"…"`), muda sempre que a representação do
  produto muda (inclusive o estoque baixado por um pedido). `If-None-Match`
  aceita lista e `*`, com comparação fraca; `If-Match`, comparação forte.
- **Idempotency-Key** em `POST /orders`: repetir a mesma chave com o mesmo
  corpo devolve **a mesma resposta** (status, corpo, `Location`) sem criar
  outro pedido; a mesma chave com outro corpo é 422; duas cópias chegando ao
  mesmo tempo nunca criam dois pedidos (a segunda recebe 409 ou espera e
  recebe o replay). Sem a chave, cada POST cria um pedido.
- **Erros** (RFC 9457): corpo com `type` (URI), `title`, `status` (igual ao
  status HTTP) e `detail` legível — sem mensagens internas (`store:`, `json:`,
  `strconv.`…).

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Evoluir uma API sem quebrar clientes antigos: mudanças aditivas, leitor tolerante e o custo de renomear um campo.
- Paginar com cursor opaco e link `next`, entendendo por que offset repete e pula itens quando a coleção muda.
- Evitar *lost updates* com ETag + `If-Match` (412/428) e gravação condicional atômica.
- Tornar um POST seguro para retentativas com `Idempotency-Key`, inclusive sob concorrência.
- Reportar erros com Problem Details (`application/problem+json`) e os códigos HTTP certos.
- Implementar GET condicional (`If-None-Match` → 304).

## Conhecimentos necessários

- Go: `net/http` (`ServeMux` com padrões `GET /products/{id}`), `encoding/json`, `sync.Mutex`.
- HTTP: métodos seguros/idempotentes, códigos 2xx/3xx/4xx, cabeçalhos.
- Recomendado: desafio-17 (servidor HTTP em Go).

## Ambiente

- Docker >= 20.10. O Go 1.25.3 roda dentro do container `golang:1.25.3-bookworm`
  (versão fixada no `Dockerfile`), sem rede: os testes sobem a API num
  `httptest.Server` dentro do próprio container. O cache de build fica no
  volume `gitquest-desafio-34-gocache` (remova com
  `docker volume rm gitquest-desafio-34-gocache` quando terminar).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker (alternativa):** com Go >= 1.25, `make` e um compilador C (o
  race detector usa cgo), rode `GITQUEST_LOCAL=1 ./start.sh` e
  `GITQUEST_LOCAL=1 ./test.sh`. O avaliador oficial é o modo Docker.

## Como começar

```bash
./start.sh
```

O script constrói a imagem, compila o código e roda a demonstração. Depois
leia `src/api/` com o contrato acima em mãos, e `internal/store/store.go` para
ver o que o "banco" oferece (e o que ele **não** garante).

Ferramentas de diagnóstico úteis:

- `./test.sh` e o log completo em `build/test.log`: cada teste mostra a
  requisição, o status, o `Content-Type` e o corpo recebidos.
- `tests/v1client_test.go`: o código do ERP do parceiro, exatamente como ele
  lê suas respostas — leia antes de mudar qualquer representação.
- `go doc net/http.ServeMux`, `go doc net/http/httptest`, e um teste seu com
  `httptest.NewServer` para fazer `GET`/`PUT` com cabeçalhos.
- RFC 9110 (seções 8.8.3 ETag, 13.1.1 If-Match, 13.1.2 If-None-Match,
  15.4.5 304, 15.5.13 412), RFC 6585 (428) e RFC 9457 (Problem Details).

## Arquivos que você pode modificar

- `src/api/` — a API. Mantenha `api.New(*store.Store) http.Handler`; pode
  criar funções, tipos e arquivos no pacote. Use apenas a biblioteca padrão.

Não altere `internal/store`, `cmd/`, `tests/`, `test.sh`, `start.sh`,
`challenge.yaml`, `go.mod`, `Dockerfile` nem o `Makefile`: o avaliador usa as
versões originais. O cliente v1 em `tests/` também não pode mudar — esse é o
ponto.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Roda `go vet ./...` e `go test -race -count=1 ./...` no container e imprime
um ✔/✘ por critério, com o motivo de cada falha. Os testes de concorrência
não dependem de sorte: um gancho do store pausa a primeira escrita **dentro**
da gravação e só então dispara a segunda requisição.

## Critérios de conclusão

- [ ] o caminho feliz do app novo continua funcionando (já funciona)
- [ ] o cliente v1 sincroniza o catálogo seguindo `next`, lê `price` em reais e faz pedidos com `quantity`; o app novo continua vendo `price_cents`/`total_cents`
- [ ] paginação por cursor opaco: sem repetições nem buracos com 3 inserções e 2 remoções no meio da travessia; o mesmo `next` devolve a mesma página
- [ ] `PUT` sem `If-Match` → 428; com ETag velho → 412; duas escritas intercaladas com o mesmo ETag → uma 200 e uma 412
- [ ] `If-None-Match` (ETag atual, lista, `W/`, `*`) → 304 sem corpo; ETag muda quando o estoque muda
- [ ] `Idempotency-Key`: replay idêntico, 422 com outro corpo, uma chave nova é outro pedido, cópias simultâneas não duplicam
- [ ] resposta perdida + retentativa do cliente v1: um único pedido, estoque baixado uma vez
- [ ] 18 cenários de erro em `application/problem+json` com 400/404/409/412/422/428 corretos e sem vazar detalhes internos
- [ ] nenhum data race; `go vet` sem alertas

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Patterns for API Design*, Olaf Zimmermann, Mirko Stocker, Daniel Lübke, Uwe Zdun e Cesare Pautasso — cap. 6, padrão **Error Report** (incluindo a nota sobre a RFC 7807, antecessora da RFC 9457); cap. 7, padrões **Pagination** (variantes offset × cursor e o problema de dados que mudam entre requisições) e **Conditional Request** (variante por *fingerprint*: ETag + `If-None-Match` + 304); cap. 8, "Introduction to API Evolution" (compatibilidade e extensibilidade) e padrão **Version Identifier** (com a menção ao "Tolerant Reader"); cap. 5, a discussão de idempotência em "Operation Responsibilities" (State Transition Operation).
- *Principles of Web API Design*, James Higginbotham — cap. 7, "Step 2: Map API Operations to HTTP Methods" (segurança e idempotência dos métodos) e "Step 3: Assign Response Codes" e "Hypermedia Controls" (paginação por link `next`); cap. 12, "API Contract Testing"; cap. 14, "API Versioning Strategies" ("Common Nonbreaking Changes" × "Incompatible Changes"); Apêndice "HTTP Primer" — "Conditional Requests" e "Concurrency Control in HTTP" (412 e 428).
- RFC 9110 — *HTTP Semantics*: 8.8.3 (ETag), 13.1.1 (If-Match), 13.1.2 (If-None-Match), 13.2.2 (ordem de avaliação das precondições). https://www.rfc-editor.org/rfc/rfc9110
- RFC 6585 — *Additional HTTP Status Codes* (428 Precondition Required). https://www.rfc-editor.org/rfc/rfc6585
- RFC 9457 — *Problem Details for HTTP APIs*. https://www.rfc-editor.org/rfc/rfc9457
- IETF draft *The Idempotency-Key HTTP Header Field* (httpapi WG): semântica de replay, 409 e 422. https://datatracker.ietf.org/doc/draft-ietf-httpapi-idempotency-key-header/

## Próximo desafio

**desafio-36 — O pedido que aceitava tudo** (trilha architecture): a API
agora protege o contrato com os clientes — mas por trás dela o modelo de
pedidos é anêmico e aceita qualquer estado. Hora de modelar agregados que
protegem as próprias invariantes.
