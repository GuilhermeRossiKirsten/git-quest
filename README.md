# desafio-31 · A precedência esquecida

> Trilha: compilers · Nível: intermediate · ~60 min · 200 pontos · Go 1.25.3 (`golang:1.25.3-bookworm`), só a biblioteca padrão

## Contexto

O motor de preços da distribuidora deixa o time comercial escrever regras como
fórmulas de planilha: `max(preco * (1 - desconto), piso)`,
`qtd >= 12 && cliente_vip || preco > 1e3`. Quem interpreta essas fórmulas é o
pacote `src/formula` — um lexer, um parser de descida recursiva e um avaliador
sobre a árvore sintática, escritos às pressas num fim de semana e "testados"
com meia dúzia de exemplos.

Nesta semana o financeiro descobriu que `100 - 10 - 5` custava 95 no sistema,
uma promoção com `-2^2` deu desconto ao contrário, e a regra
`qtd != 0 && total / qtd > 10` nem chegou a ser cadastrada. Você herdou o
pacote e a pilha de chamados.

## Problema

Sintomas relatados (rode `./start.sh`: a demonstração mostra *esperado* ×
*obtido* para cada chamado; `./calc.sh 'fórmula'` mostra a AST com todos os
parênteses explícitos):

- Subtrações e divisões encadeadas dão valores estranhos (`100 - 10 - 5` → 95,
  `100 / 10 / 5` → 50); `2 ^ 3 ^ 2` dá 64; `-2 ^ 2` dá 4.
- `1.5e3 + 1` vale 1.5 — sem erro nenhum; `.5` e `1.` são recusados.
- Fórmulas com `<=`, `>=`, `==`, `!=`, `&&` ou `||` são recusadas com
  "caractere inesperado".
- `preco 2` é aceita e vale 80; o resto da fórmula é ignorado em silêncio.
- Erros em fórmulas de várias linhas apontam linha e coluna erradas, e a
  mensagem do erro (`err.Error()`) não diz onde ele está.
- `preco / qtd` com `qtd = 0` devolve `+Inf`; uma variável com o nome errado
  vale 0; `min()` derruba o processo com um pânico.
- Uma fórmula colada de um gerador com milhares de `(` derruba o serviço com
  *stack overflow*.

Seu trabalho é fazer o pacote cumprir o contrato abaixo — investigando e
corrigindo o código existente, não reescrevendo do zero.

**Léxico**

- Espaço, tabulação, `\r` e `\n` separam tokens. Linhas são separadas por
  `\n`; linha e coluna começam em 1 e a coluna conta caracteres (runas).
- Número: `dígitos ('.' dígitos?)? expoente?` ou `'.' dígitos expoente?`, com
  `expoente = [eE] [+-]? dígitos` — `1.5e3`, `.5`, `1.`, `2E-2` são válidos.
  Um número é malformado se o expoente não tiver dígitos (`1e`, `1e+`), se for
  só `.`, se estiver colado a letra, dígito, `_` ou `.` (`12abc`, `1.2.3`,
  `1_000`) ou se não couber num `float64` (`1e400`): erro **`KindLex`** na
  posição do **primeiro caractere** do número.
- Identificador: `[A-Za-z_][A-Za-z0-9_]*`.
- Operadores: `+ - * / % ^ ( ) ,` `< <= > >= == != && || !` — sempre o mais
  longo possível (`a<-b` é `a < -b`). Qualquer outro caractere (inclusive `=`,
  `&` ou `|` sozinhos e não-ASCII) é erro `KindLex` na posição dele.

**Sintaxe e precedência** (da menor para a maior)

| Nível | Operadores | Associatividade |
|---|---|---|
| 1 | `\|\|` | esquerda |
| 2 | `&&` | esquerda |
| 3 | `==` `!=` | esquerda |
| 4 | `<` `<=` `>` `>=` | esquerda |
| 5 | `+` `-` | esquerda |
| 6 | `*` `/` `%` | esquerda |
| 7 | `-` `!` unários (prefixo) | — |
| 8 | `^` | **direita**; o operando direito pode ser unário |

Logo `2-3-4 = (2-3)-4`, `2^3^2 = 2^(3^2)`, `-2^2 = -(2^2)`, `2^-1 = 0.5`,
`1 < 2 < 3 = (1<2)<3`. Átomos: número, variável, `nome(arg, ...)` (zero ou
mais argumentos) e `( expr )`.

- `Parse` precisa consumir a **fórmula inteira**: um token que sobra é erro
  **`KindSyntax`** na posição dele (`1 2` → 1:3). Token inesperado → erro na
  posição do token; fim inesperado → erro na posição logo **depois do último
  caractere** da fórmula (`1 +` → 1:4; `1 +\n` → 2:1; fórmula vazia → 1:1).
- **Profundidade**: `MaxDepth = 200`. Abrem um nível, até a subexpressão
  que introduzem terminar: `(` de agrupamento, `(` de chamada, operador
  unário e o operando direito de `^`. O token que abriria o nível 201 gera
  erro **`KindTooDeep`** na posição dele — sem pânico e sem estourar a pilha,
  qualquer que seja o tamanho da entrada. Uma soma com milhares de termos não
  é aninhamento e deve funcionar.

**Avaliação** (`Eval`)

- Verdadeiro é ≠ 0; comparações, `!`, `&&` e `||` devolvem 1 ou 0. `&&` e
  `||` fazem **curto-circuito**: o lado direito só é avaliado quando o esquerdo
  não decide. Os demais operandos e os argumentos de funções são avaliados da
  esquerda para a direita.
- `%` é `math.Mod`, `^` é `math.Pow`. Divisão ou resto por zero, ou uma
  operação cujo resultado seja NaN/±Inf (`10 ^ 400`, `(-8) ^ 0.5`), é erro
  **`KindEval`** na posição do **operador**.
- Variável fora de `Env.Vars` → `KindEval` na posição do nome. Funções:
  `Env.Funcs` primeiro, depois as embutidas `min`/`max` (1 ou mais argumentos)
  e `abs` (exatamente 1). Função desconhecida, número de argumentos inválido ou
  erro devolvido por uma `Func` → `KindEval` na posição do nome da função; o
  erro da `Func` fica acessível por `errors.Is`/`errors.As`.

**Erros**: todo erro de `Parse` e `Eval` é um `*formula.Error` com `Pos`,
`Kind` e `Msg`; `Error()` devolve `"linha:coluna: mensagem"`.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Escrever um analisador léxico à mão: maior casamento possível, leitura adiante, fronteiras de token e posições linha:coluna.
- Traduzir uma tabela de precedência e associatividade em código: descida recursiva por níveis ou *precedence climbing*.
- Posicionar operadores unários em relação à potência e entender por que `-2^2` é `-4`.
- Exigir que o parser consuma a entrada inteira e reportar erros com posição exata.
- Limitar a profundidade de recursão para que entradas hostis não derrubem o processo.
- Implementar curto-circuito e erros de avaliação sem pânicos.
- Verificar um parser com testes de propriedade (imprimir e reler) e fuzzing.

## Conhecimentos necessários

- Go: structs, interfaces, `switch` de tipos, `errors.As`/`errors.Is`.
- Noções de gramática livre de contexto: produções, derivação, árvore sintática.
- Recursão. Nenhum desafio anterior é pré-requisito.

## Ambiente

- Docker >= 20.10. O Go 1.25.3 roda dentro do container `golang:1.25.3-bookworm`
  (versão fixada no `Dockerfile`), sem rede. O cache de build fica no volume
  `gitquest-desafio-31-gocache` (remova com
  `docker volume rm gitquest-desafio-31-gocache` quando terminar).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker (alternativa):** com Go >= 1.25 e `make`, use
  `GITQUEST_LOCAL=1 ./start.sh`, `GITQUEST_LOCAL=1 ./calc.sh '…'` e
  `GITQUEST_LOCAL=1 ./test.sh`. O avaliador oficial é o modo Docker.

## Como começar

```bash
./start.sh
```

O script constrói a imagem, compila e roda a demonstração dos chamados. Depois
leia `src/formula/lexer.go`, `parser.go` e `eval.go` com o contrato acima em mãos.

Ferramentas de diagnóstico úteis:

- `./calc.sh '2 - 3 - 4'` e `./calc.sh 'preco / qtd' preco=80 qtd=0`: mostram
  a AST com todos os parênteses (`(2 - (3 - 4))` denuncia o agrupamento) e o
  valor ou o erro com `Kind` e posição.
- `./test.sh` e o log completo em `build/test.log`: cada caso diz a fórmula, o
  esperado e o obtido. Os testes de propriedade e de fuzz usam sementes fixas:
  a mesma falha aparece sempre na mesma entrada.
- `go vet ./...` (já roda no `./test.sh`).
- Testes seus: crie `src/formula/*_test.go` (pacote `formula`) para testar o
  lexer diretamente — `src/` é seu.

## Arquivos que você pode modificar

- `src/formula/` — lexer, parser, avaliador e erros. Mantenha a API pública
  usada pelos testes: `Parse`, `Eval`, `Env`, `Func`, os nós `Number`, `Var`,
  `Unary`, `Binary`, `Call` (com seus campos), `Expr`, `Op` e suas constantes,
  `Pos`, `Error`, `Kind` e suas constantes, `MaxDepth`. Pode criar funções,
  tipos e arquivos. Use apenas a biblioteca padrão.

Não altere `tests/`, `cmd/`, `test.sh`, `start.sh`, `calc.sh`,
`challenge.yaml`, `go.mod`, `Dockerfile` nem o `Makefile`: o avaliador usa as
versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Roda `go vet ./...` e `go test -count=1 -timeout 240s ./...` no container e
imprime um ✔/✘ por critério, com os casos que falharam.

## Critérios de conclusão

- [ ] fórmulas simples continuam funcionando (o caminho feliz — já funciona)
- [ ] números com fração, expoente, `.5` e `1.` são aceitos; números malformados são `KindLex` no primeiro caractere
- [ ] operadores de dois caracteres são reconhecidos (maior casamento); `=`, `&`, `|` sozinhos são `KindLex`
- [ ] erros apontam linha:coluna exatas (várias linhas, CRLF, fim da entrada) e `Error()` começa com `"linha:coluna: "`
- [ ] a precedência entre níveis segue a tabela
- [ ] associatividade à esquerda nos níveis 1–6, `^` à direita, unário abaixo de `^`
- [ ] a fórmula inteira é consumida; sobras, vazios e entradas incompletas são `KindSyntax` na posição certa
- [ ] `&&`/`||` com curto-circuito (efeitos colaterais observados por uma `Func` de teste) e ordem de avaliação da esquerda para a direita
- [ ] erros de avaliação (divisão por zero, nomes desconhecidos, aridade, resultado não finito, erro de `Func`) com posição
- [ ] propriedade: para 4000 ASTs aleatórias (semente fixa), `Parse(imprimir com parênteses mínimos)` devolve a mesma AST, posições incluídas, e `Eval` concorda com um avaliador de referência
- [ ] fuzz determinístico com 20 000 entradas: nenhum pânico, todo erro é `*formula.Error` com posição dentro da entrada
- [ ] aninhamento acima de `MaxDepth` é `KindTooDeep` na posição certa, inclusive com 1 000 000 de `(` num processo com pilha limitada a 128 MiB
- [ ] `go vet` sem alertas

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Compiladores: Princípios, Técnicas e Ferramentas*, 2ª ed., Aho, Lam, Sethi e Ullman — cap. 2, seções 2.2.4–2.2.6 (ambiguidade, associatividade e precedência de operadores; a gramática `expr`/`term`/`factor` gerada a partir de uma tabela de precedência), 2.4.5 (recursão à esquerda e por que ela trava a descida recursiva) e 2.6.1–2.6.3 (análise léxica: espaços, número de linha, leitura adiante para `>=`, constantes); cap. 3, seções 3.1.4 (erros léxicos), 3.3.4–3.3.5 (definição regular de `number` com fração e expoente opcionais — Exemplos 3.5 e 3.7) e 3.4.3 (diagrama de transição para números); cap. 4, seções 4.1.3–4.1.4 (tratamento de erros: informar o local exato; modo pânico), 4.3.2–4.3.3 (eliminando ambiguidade e recursão à esquerda) e 4.4.1 (descida recursiva: o procedimento do símbolo inicial só anuncia sucesso se consumir a entrada inteira).
- *Writing a C Compiler*, Nora Sandler — cap. 1, "The Lexer" (Listing 1-3: maior casamento e fronteira de palavra; por que `123bar` é erro léxico); cap. 2 (operadores unários); cap. 3, "The Trouble with Recursive Descent Parsing", "The Adequate Solution: Refactoring the Grammar" e "The Better Solution: Precedence Climbing" (Listings 3-5 a 3-7, Tabela 3-1); cap. 4, "Short-Circuiting Operators" e "The Lexer" (tokens de dois caracteres, sempre o mais longo); cap. 5, "An Improved Precedence Climbing Algorithm" (Listing 5-8: operador associativo à direita chamando com `precedence(op)` em vez de `precedence(op) + 1`).
- Documentação do Go: `strconv.ParseFloat` (https://pkg.go.dev/strconv#ParseFloat), `math.Pow`/`math.Mod` (https://pkg.go.dev/math), `runtime/debug.SetMaxStack` (https://pkg.go.dev/runtime/debug#SetMaxStack).

## Próximo desafio

Por enquanto este é o único desafio da trilha *compilers* — não há um
próximo encadeado. Veja o catálogo com `./scripts/gitquest list` (ou
`./scripts/gitquest list --track compilers` quando novos desafios da trilha
chegarem) e escolha a próxima fase.
