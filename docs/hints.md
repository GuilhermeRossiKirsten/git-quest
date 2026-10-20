# Dicas — desafio-6

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Na sondagem linear, uma busca anda slot a slot a partir do slot inicial da chave.
**O que faz a busca parar?** Agora imagine três chaves que colidiram e ocupam
slots consecutivos: o que acontece com a busca da terceira se a do meio for
apagada? Desenhe uma tabela de 8 slots no papel e simule.

Faça o mesmo exercício com `size()`: em quais caminhos de `insert_or_assign`
ele deveria mudar?

## Nível 2

Onde olhar em `src/flat_map.hpp`:

- `erase` e o `enum class State` — existe um estado que ninguém usa.
- `insert_or_assign` — a condição de crescimento é avaliada *antes* de inserir; faça a conta para `capacity() == 8`.
- `grow` — quais slots ele reinsere? Depois que `erase` mudar, essa resposta continua certa?
- `home` — para chaves `id << 32`, quais bits sobram depois de `& mask()`? `std::hash<std::uint64_t>` na libstdc++ é a identidade.

Corrigir `erase` muda as regras: `insert_or_assign` não pode mais parar no primeiro
slot "livre" do caminho, e um slot apagado continua ocupando lugar na tabela.

## Nível 3

Abordagem (uma das possíveis):

1. `erase` deixa uma **lápide** (`Deleted`): a busca passa por ela, a inserção pode reaproveitá-la —
   mas só depois de percorrer o cluster até um slot `Empty` e confirmar que a chave não existe adiante.
   (Alternativa sem lápides: *backward-shift deletion*, que puxa os elementos seguintes do cluster para trás.)
2. Conte as lápides separadamente. Decida o rehash pelo total ocupado (vivos + lápides + 1 ≤ 0,75 · capacidade),
   garantindo que sempre exista um slot `Empty` — senão a busca de uma chave ausente nunca termina.
3. No rehash, reinsira **só** os elementos vivos e escolha a nova capacidade a partir do número de vivos
   (por exemplo, a menor potência de 2 em que eles ocupem no máximo metade): assim, ciclos de insere/apaga
   limpam lápides sem dobrar a memória a cada vez.
4. `size_` só aumenta quando a chave é nova.
5. Misture os bits do hash antes de aplicar a máscara (finalizador do splitmix64, ou multiplicação de
   Fibonacci usando os bits **altos** do produto).
