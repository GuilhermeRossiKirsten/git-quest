# Objetivos de aprendizagem — desafio-31

Ao concluir este desafio você será capaz de:

1. Escrever um analisador léxico à mão: maior casamento possível (`<=` antes
   de `<`), leitura adiante, definição regular de números com fração e
   expoente, fronteira de token e erros léxicos com posição.
2. Manter posições linha:coluna corretas ao longo da análise e reportá-las em
   todo erro, inclusive no fim inesperado da entrada.
3. Traduzir uma tabela de precedência e associatividade em código, por
   descida recursiva com um não-terminal por nível ou por *precedence
   climbing*, sabendo por que a recursão à direita inverte a associatividade.
4. Posicionar operadores unários em relação à potência (`-2^2 = -4`,
   `2^-1 = 0.5`) e implementar um operador associativo à direita.
5. Exigir que o parser consuma a entrada inteira e detectar sobras.
6. Proteger um parser recursivo contra entradas hostis com um limite de
   profundidade, devolvendo um erro limpo em vez de estourar a pilha.
7. Implementar a avaliação com curto-circuito, ordem de avaliação definida e
   erros de execução (divisão por zero, nomes desconhecidos, aridade,
   resultados não finitos) sem pânicos.
8. Validar um parser com testes de propriedade (imprimir com parênteses
   mínimos e reler) e com fuzzing determinístico.

## Como os testes verificam cada objetivo

| Objetivo | Verificação (`tests/`) |
|---|---|
| 1. Lexer | `TestLexico_Numeros`: `1.5e3`, `.5`, `1.`, `1.e2`, `2e-2`, `2.5e+2`… com o valor exato; `1e`, `1e+`, `1.2.3`, `12abc`, `.`, `1e400`, `1_000` → `KindLex` no 1º caractere do número. `TestLexico_Operadores`: `<= >= == != && \|\| !`, `x<-y` (maior casamento), `=`/`&`/`\|` sozinhos → `KindLex` na posição exata, `1 <> 2` → `KindSyntax` em 1:4. |
| 2. Posições | `TestPosicoes`: erros em fórmulas de várias linhas (2:3, 3:5, fim em 2:2, 3:6, 2:1, 3:1), CRLF, caractere não-ASCII, erros de avaliação na 2ª linha; `Error()` começa com `"2:3: "`. |
| 3. Precedência | `TestPrecedencia`: 19 fórmulas que só dão o valor certo com os 8 níveis da tabela (`2 == 1 < 3` = 0, `1 \|\| 0 && 0` = 1, `1 < 2 && 3 < 2` = 0…). `TestPropriedade_ImprimirERelerAST`: 4000 ASTs aleatórias (PCG(31, 2026)) impressas com parênteses mínimos segundo a tabela; `Parse` precisa devolver a mesma árvore, com as posições de todos os nós. |
| 4. Unários e `^` | `TestAssociatividadeEUnarios`: `2-3-4`, `2-3+4`, `100/10/5`, `17%5%3`, `2^3^2` = 512, `2^-1^2`, `-2^2` = -4, `(-2)^2` = 4, `-x^2`, `--2`, `3 > 2 > 1` = 0. A propriedade também gera `^` com operandos unários. |
| 5. Entrada inteira | `TestEntradaInteira`: `1 2` → 1:3, `(1) (2)`, `max(1, 2) x`, `1.5 e3`, `(1 + 2))`, vazio → 1:1, só espaços → 1:4, `1 +` → 1:4, `max(1,)`, `max(,1)`, `+1`. |
| 6. Profundidade | `TestProfundidadeMaxima`: 200 níveis de `(`, `-`, `abs(` e `^` são aceitos; o 201º → `KindTooDeep` exatamente na posição do token (1:201, 1:804, 201:1, 1:402). Num processo filho com `debug.SetMaxStack(128 MiB)`: 1 000 000 de `(`, de `-` e de `max(` precisam dar `KindTooDeep` sem estourar a pilha, e somas/subtrações com 20 000 termos precisam dar o valor certo. |
| 7. Avaliação | `TestCurtoCircuito`: uma `Func` de teste (`tick`) registra as chamadas — `0 && tick(1)` não chama, `tick(1) && tick(0) && tick(3)` chama `[1 0]`, `qtd != 0 && 10 / qtd > 2` não divide; operandos e argumentos avaliados da esquerda para a direita. `TestErrosDeAvaliacao`: divisão/resto por zero, `10 ^ 400`, `(-8) ^ 0.5`, variável e função desconhecidas, `min()`, `abs(1, 2)` → `KindEval` na posição do operador ou do nome; erro de `Func` preservado para `errors.Is`. A propriedade compara `Eval` com um avaliador de referência nas 4000 árvores. |
| 8. Propriedade e fuzz | `TestPropriedade_ImprimirERelerAST` (acima) e `TestFuzz_SemPanico`: 20 000 entradas (PCG(7, 31)) montadas com fragmentos válidos e inválidos; nenhum pânico em `Parse`/`Eval`, todo erro é `*formula.Error` de tipo coerente e posição dentro da entrada, e toda AST aceita sobrevive a imprimir-e-reler. |
| Transversal | `TestCaminhoFeliz` (o que já funcionava continua funcionando) e `go vet ./...` sem alertas. |
