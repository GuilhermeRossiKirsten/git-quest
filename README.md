# desafio-36 · O pedido que aceitava tudo

> Trilha: architecture · Nível: intermediate · ~60 min · 200 pontos · Go 1.25.3 (`golang:1.25.3-bookworm`, só biblioteca padrão)

## Contexto

A loja cresceu, e o pacote de pedidos virou terra de ninguém. As structs
nasceram como espelho das tabelas do banco; as regras de negócio foram
aparecendo nos handlers do serviço de aplicação, uma de cada vez, conforme os
incidentes aconteciam. Na última sprint a equipe criou métodos (`NewOrder`,
`AddLine`, `Confirm`...) para que os outros times parassem de mexer direto nos
campos — mas os campos continuam exportados, e o time de faturamento já
importou o pacote e altera `o.State` "só para um ajuste rápido".

Esta semana o suporte abriu cinco chamados:

- um cliente com limite de R$ 100 fechou um pedido de R$ 500;
- um pedido **já confirmado** recebeu mais itens pelo app novo, que chama o
  domínio sem passar pelo serviço;
- o cupom de 10% saiu como 19% no boleto;
- o e-mail "seu pedido foi confirmado" chegou para um pedido que nunca foi
  gravado (o banco estava fora do ar);
- a soma dos itens de um carrinho dá R$ 0,28 para um café de R$ 0,29.

## Problema

Rode `./start.sh`: a demonstração (`cmd/demo`) reproduz os chamados e marca
`DIVERGE` em cada um. Sintomas observáveis:

- `money.New(29, "BRL").Cents()` devolve 28; `0.1 + 0.2` não é igual a `0.3`;
  somar `a.Add(b)` muda o próprio `a`; somar reais com dólares "funciona".
- Um pedido aceita itens com quantidade zero, preço em outra moeda, SKU
  repetido e acima do limite de crédito — desde que quem chame não seja o
  serviço de aplicação (que confere *algumas* dessas regras).
- Depois de mudar a quantidade de um item, o total continua o antigo.
- O desconto é descontado de novo na confirmação.
- Confirmar duas vezes, cancelar pedido enviado: tudo aceito.
- `PullEvents()` devolve os mesmos eventos de novo a cada chamada; comandos
  rejeitados deixam eventos para trás; o serviço publica antes de gravar.
- Qualquer pacote consegue escrever `o.State = ordering.StatusShipped`.

Seu trabalho: transformar `ordering.Order` num **agregado** que protege as
próprias invariantes, e `money.Money` num **value object** — investigando e
corrigindo o código existente, mantendo a API pública abaixo.

### Contrato (API pública usada pelos testes)

Os testes usam **somente** estes nomes. Você pode criar o que quiser por dentro
(tipos, funções, arquivos), mas eles precisam continuar existindo com estas
assinaturas.

**Pacote `desafio36/src/money`**

| API | Comportamento |
|---|---|
| `New(cents int64, currency string) (Money, error)` | moeda = 3 letras maiúsculas (`ErrInvalidCurrency`); centavos negativos são permitidos |
| `MustNew(cents, currency) Money`, `Zero(currency) Money` | atalhos |
| `Parse(amount, currency string) (Money, error)` | decimal com no máximo 2 casas: `"12.34"`, `"-0.05"`, `"7"`; qualquer outra coisa (`"1.005"`, `"1e3"`, `"12,34"`, `"NaN"`...) → `ErrInvalidAmount` |
| `Cents() int64`, `Currency() string`, `IsZero() bool`, `Equal(Money) bool`, `String()` | `String()` → `"BRL 12.34"`, `"BRL -0.05"` |
| `Add(Money)`, `Sub(Money)` `(Money, error)` | moedas diferentes → `ErrCurrencyMismatch` |
| `Multiply(n int64) Money` | preço × quantidade |
| `Percent(p int64) Money` | p% arredondado ao centavo mais próximo; meio centavo se afasta do zero (12,5 → 13; −12,5 → −13) |

`Money` é um valor: dois `Money` com mesmos centavos e moeda são iguais por
`==` (e servem como chave de map), todas as operações têm receptor por valor e
devolvem um valor novo, e nenhum campo é visível fora do pacote.

**Pacote `desafio36/src/ordering`**

| API | Regra |
|---|---|
| `NewOrder(id, customerID string, creditLimit money.Money) (*Order, error)` | id e cliente não vazios, limite > 0 com moeda válida (senão `ErrInvalidArgument`, pedido `nil`). A moeda do pedido é a do limite. Status `draft`. Evento `OrderCreated` |
| `AddLine(sku string, qty int, unitPrice money.Money) error` | só em `draft` (`ErrNotDraft`); SKU não vazio e preço > 0 (`ErrInvalidArgument`); 1 ≤ qty ≤ 10000 (`ErrInvalidQuantity`); preço na moeda do pedido (`money.ErrCurrencyMismatch`); SKU ainda não presente (`ErrDuplicateSKU`); o total resultante não passa do limite de crédito (`ErrCreditLimitExceeded`). Evento `LineAdded` |
| `ChangeQuantity(sku string, qty int) error` | só em `draft`; item existente (`ErrLineNotFound`); mesma regra de quantidade e de crédito. Evento `LineQuantityChanged` |
| `ApplyDiscount(percent int) error` | só em `draft`; 1 a 100 (`ErrInvalidDiscount`); um cupom por pedido (`ErrDiscountAlreadyApplied`). Evento `DiscountApplied` |
| `Confirm() error` | só em `draft`; ao menos um item (`ErrEmptyOrder`). Evento `OrderConfirmed{Total}` com o total do pedido |
| `Ship() error` | só em `confirmed` (`ErrNotConfirmed`). Evento `OrderShipped` |
| `Cancel(reason string) error` | em `draft` ou `confirmed`; enviado → `ErrAlreadyShipped`; cancelado → `ErrAlreadyCancelled`. Evento `OrderCancelled` |
| `ID()`, `CustomerID()`, `Status()`, `Currency()`, `CreditLimit()` | leitura |
| `Lines() []OrderLine` | itens na ordem de inclusão; `OrderLine` tem `SKU()`, `Quantity()`, `UnitPrice()`, `Total()` |
| `Subtotal()`, `Discount()`, `Total()` `money.Money` | subtotal = Σ preço × qty; desconto = `Subtotal().Percent(cupom)`, sempre sobre o subtotal **atual**; total = subtotal − desconto |
| `PullEvents() []Event` | eventos registrados desde a última chamada, em ordem; cada evento é entregue uma única vez |

Regras transversais: um comando **rejeitado** devolve um erro reconhecível por
`errors.Is` (pode embrulhar com `%w`), não altera nada e não registra evento;
um comando **aceito** registra exatamente um evento. O total nunca passa do
limite de crédito. Os tipos e erros sentinela já estão declarados em
`src/ordering/events.go` e `src/ordering/errors.go`.

**Pacote `desafio36/src/app`** — `NewService(Repository, Publisher)` e os
métodos `PlaceOrder`, `AddLine`, `ApplyDiscount`, `Confirm`, `Ship`, `Cancel`:
cada um carrega o pedido, executa um comando do agregado, salva e **só depois
de salvar com sucesso** publica os eventos. Erros do domínio e do repositório
voltam reconhecíveis por `errors.Is`.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Mover invariantes de negócio dos serviços para o agregado, que se torna a única porta de mudança.
- Encapsular o estado em Go (campos não exportados, cópias defensivas) e provar o encapsulamento com testes "não deve compilar".
- Modelar dinheiro como value object imutável em centavos inteiros, com moeda e arredondamento explícitos.
- Calcular valores derivados a partir do estado, em vez de mantê-los em cache.
- Registrar eventos de domínio só para comandos aceitos e publicá-los só depois do commit.
- Verificar invariantes com testes de propriedade determinísticos.

## Conhecimentos necessários

- Go: structs, métodos com receptor por valor × ponteiro, visibilidade por
  letra maiúscula, slices (aliasing e `append`), `errors.Is` e `%w`.
- Noções de DDD: entidade, agregado, value object, evento de domínio.
- Por que `float64` não representa centavos exatamente.

## Ambiente

- Docker >= 20.10. O Go 1.25.3 roda no container `golang:1.25.3-bookworm`
  (versão fixada no `Dockerfile`), sem rede. O cache de build fica no volume
  `gitquest-desafio-36-gocache` (remova com
  `docker volume rm gitquest-desafio-36-gocache` quando terminar).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker (alternativa):** com Go >= 1.25 e `make` instalados, rode
  `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`. O avaliador
  oficial é o modo Docker.

## Como começar

```bash
./start.sh
```

Constrói a imagem, compila e roda `cmd/demo`. Depois leia, nesta ordem,
`src/money/money.go`, `src/ordering/order.go` e `src/app/service.go`, com o
contrato acima em mãos. Para cada regra do contrato pergunte: *onde* ela é
verificada hoje, e *quem* consegue contorná-la?

Ferramentas de diagnóstico úteis:

- `./test.sh` e o log completo em `build/test.log` (cada falha diz o que esperava).
- `go test -run 'TestInvariantes_Itens/SKU' -v ./tests/` para rodar um caso só
  (dentro do container: veja o `Makefile`, ou use o modo local).
- O teste de propriedade imprime semente, sequência e os últimos comandos —
  reproduza a sequência à mão com o `cmd/demo` ou num teste seu.
- `go doc desafio36/src/ordering` e `go vet ./...`.

## Arquivos que você pode modificar

- `src/` — os pacotes `money`, `ordering` e `app`. Mantenha a API pública do
  contrato; pode criar tipos, funções e arquivos auxiliares. Só biblioteca padrão.

Não altere `tests/`, `cmd/`, `test.sh`, `challenge.yaml`, `go.mod`, `Dockerfile`
nem o `Makefile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Roda `go vet ./...` e `go test -count=1 -timeout 180s ./...` no container e
imprime um ✔/✘ por critério. O teste de encapsulamento compila pequenos
programas-sonda contra o seu código: um de controle (só a API pública, tem de
compilar) e vários que tentam alterar campos por fora (não podem compilar).

## Critérios de conclusão

- [ ] `Money` em centavos: `0.1 + 0.2 == 0.3`, `Parse` exato, moedas não se misturam, `Percent` arredonda meio-para-cima
- [ ] `Money` é value object: operações não alteram operandos, receptor por valor, igualdade por `==`
- [ ] invariantes de itens (quantidade, preço, moeda, SKU único, limite de crédito) valem para qualquer chamador
- [ ] ciclo de vida: confirmar, enviar, cancelar e descontar só nas transições permitidas
- [ ] subtotal, desconto e total sempre coerentes com os itens atuais; desconto incide uma vez
- [ ] código de fora do pacote não compila ao tentar alterar o estado do pedido ou do `Money`
- [ ] sem campos exportados em `Order`, `OrderLine` e `Money`; `Lines()` e `PullEvents()` devolvem cópias
- [ ] exatamente um evento por comando aceito, nenhum por rejeitado, cada um entregue uma vez
- [ ] o serviço de aplicação publica só depois de salvar e propaga os erros do domínio
- [ ] 400 sequências aleatórias de comandos (semente fixa) nunca violam as invariantes
- [ ] `go vet` sem alertas

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Domain-Driven Design Distilled*, Vaughn Vernon — cap. 5, "Tactical Design with Aggregates": "Why Used" (o agregado como fronteira de consistência transacional), "Aggregate Rules of Thumb" (Regra 1: proteger invariantes dentro da fronteira; Regra 3: referenciar outros agregados só pela identidade), "Modeling Aggregates" (o Anemic Domain Model e a lógica que vaza para os Application Services; métodos de comportamento em vez de setters públicos), o quadro "What Is a Value Object?" e "Testable Units".
- *Domain-Driven Design Distilled*, Vaughn Vernon — cap. 6, "Tactical Design with Domain Events": "Designing, Implementing, and Using Domain Events" (nomes no passado; salvar agregado e evento na mesma transação; um comando pode ser rejeitado, um evento é história).
- *Balancing Coupling in Software Design*, Vlad Khononov — cap. 7, "Integration Strength": "Intrusive Coupling" (depender de detalhes de implementação que não foram feitos para integração) e "Functional Coupling" (a mesma regra implementada em dois módulos — *symmetric functionality*); cap. 6, "Connascence of Meaning".
- *Practical Object-Oriented Design*, 2ª ed., Sandi Metz — cap. 2, seção 2.3.1 "Depend on Behavior, Not Data" ("Hide Instance Variables"); cap. 4, seções 4.3.4 "Asking for 'What' Instead of Telling 'How'" e 4.4.1 "Create Explicit Interfaces"; cap. 9, seções 9.1.2 "Knowing What to Test" e 9.2.2 "Proving the Public Interface".
- Martin Fowler — "AnemicDomainModel": https://martinfowler.com/bliki/AnemicDomainModel.html
- Martin Fowler — "Money" (Patterns of Enterprise Application Architecture): https://martinfowler.com/eaaCatalog/money.html
- Especificação da linguagem Go — "Exported identifiers" e "Method sets": https://go.dev/ref/spec#Exported_identifiers

## Próximo desafio

Este é, por enquanto, o último desafio da trilha **architecture**. Volte ao
`./scripts/gitquest status` para escolher o próximo — e, se quiser ir além,
experimente trocar o `Publisher` do serviço por um *outbox* gravado na mesma
transação do pedido.
