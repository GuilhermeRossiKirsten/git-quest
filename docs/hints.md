# Dicas — desafio-31

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Antes de ler o código, rode `./calc.sh '2 - 3 - 4'`, `./calc.sh '2 ^ 3 ^ 2'`
e `./calc.sh '-2 ^ 2'` e compare a AST impressa com a tabela de precedência do
README. Para cada fórmula, pergunte: qual operador deveria estar na raiz da
árvore? Qual está?

Depois leia o lexer como uma máquina que olha um caractere por vez: o que ele
faz quando vê `<` seguido de `=`? E `1.5` seguido de `e3`? Onde ele guarda a
linha?

Por fim, o parser: quando `Parse` devolve uma AST, quem garante que **todos**
os tokens foram usados? E o que impede uma recursão de um milhão de níveis?

## Nível 2

Lugares para olhar:

- `parser.go`, `sum` e `term`: `sum := term [('+'|'-') sum]` é recursiva **à
  direita** — que árvore ela constrói para `a - b - c`? (Aho et al., seção
  2.2.5; Sandler, cap. 3, observa que `<exp> ::= <term> "+" <exp>` produz uma
  árvore associativa à direita.)
- `expr` coloca `<`, `==`, `&&` e `||` num único laço: todos na mesma
  precedência.
- `power` usa um laço (associa à esquerda) e chama `unary` dos dois lados: o
  `-` de `-2 ^ 2` é consumido antes de o `^` aparecer.
- `Parse` devolve `p.expr()` sem olhar o próximo token.
- `lexer.go`: o `Pos` nasce de `i + 1` (bytes, linha sempre 1); só existe uma
  lista de operadores de **um** caractere; o número para no primeiro caractere
  que não é dígito nem ponto — e não verifica o que vem colado depois.
- `eval.go`: `evalBinary` avalia os dois lados antes de olhar o operador;
  `env.Vars[n.Name]` devolve 0 para nomes ausentes; `min` lê `a[0]` sem saber
  se existe.
- `errors.go`: `go vet` aponta a linha de `Error()`.

## Nível 3

Uma abordagem possível, em passos pequenos (rode `./test.sh` entre eles):

1. **Lexer.** Percorra a fórmula como `[]rune` e mantenha `line`/`col`
   atualizados numa função `advance` (em `\n`: linha + 1, coluna 1). Antes
   dos operadores de um caractere, tente os de dois (`<=`, `>=`, `==`, `!=`,
   `&&`, `||`). Reconheça números pela definição regular do README (Aho,
   Exemplos 3.5 e 3.7): dígitos, fração opcional, expoente opcional que
   **exige** dígitos; no fim, letra/dígito/`_`/`.` colado torna o número
   malformado. O token de fim fica na posição após o último caractere.
2. **Precedence climbing** (Sandler, Listing 3-7): uma tabela operador →
   precedência para os níveis 1–6 e `expr(minPrec)` que, num laço, consome
   operadores com precedência `>= minPrec` e chama `expr(prec + 1)` para o
   operando direito — isso dá associatividade à esquerda em todos os níveis.
3. **Unário e potência.** `unary := ('-'|'!') unary | power` e
   `power := primary ['^' unary]`. Assim `-2^2 = -(2^2)`, `2^3^2 = 2^(3^2)`
   (recursão à direita, a mesma ideia do Listing 5-8) e `2^-1` é aceito.
4. **Fim da entrada.** Depois de `expr(1)`, se o próximo token não for o fim,
   erro de sintaxe na posição dele.
5. **Profundidade.** Um contador no parser: incremente ao abrir `(`, chamada,
   unário e o lado direito de `^`; se passar de `MaxDepth`, devolva
   `KindTooDeep` na posição do token; decremente ao fechar.
6. **Avaliação.** `&&`/`||`: avalie a esquerda e só avalie a direita se for
   preciso. Divisão/resto por zero, NaN/Inf, variável ausente e aridade viram
   `*Error` com `KindEval` na posição pedida.

## Nível 4

Checklist final, item a item:

- O laço de precedence climbing **não** pode incluir `^`: ele é associativo à
  direita e mais forte que o unário; trate-o em `power`.
- A posição de um `Binary` é a do **operador**; a de um `Call`, a do nome.
  O teste de propriedade compara as posições de todos os nós.
- O erro de fim inesperado aponta para depois do último caractere, inclusive
  espaços e quebras de linha finais (`1 +\n` → 2:1).
- `\r` é espaço; só `\n` muda de linha.
- O contador de profundidade precisa contar o unário: `-----…1` com um milhão
  de `-` também estoura a pilha.
- Uma soma com 20 000 termos não é aninhamento: monte-a no laço, sem uma
  recursão por termo.
- O erro de uma `Func` deve ir no campo `Err` do `*Error` (para `errors.Is`).
