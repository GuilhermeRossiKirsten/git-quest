# Dicas — desafio-12

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Uma função ser uma corrotina não a torna preguiçosa: o que acontece entre o
`src.open()` e o primeiro `co_yield` de `lines()`? Quantas vezes `read()` é
chamado antes de o consumidor receber a primeira linha?

E quanto ao que é produzido: para onde apontam os `std::string_view` de um
`Record` depois que o generator que os produziu foi destruído — por exemplo,
quando `first_matching()` retorna?

Por fim, procure todo `catch` no código. Quem decidiu que o consumidor não
precisava saber daquele erro?

## Nível 2

- Um `std::generator` suspenso que é destruído **não volta a executar**: o
  código depois do último `co_yield` nunca roda. Mas os destrutores das
  variáveis locais do quadro da corrotina rodam. Qual dos dois mecanismos
  garante o `close()`?
- Uma exceção que sai do corpo de um `std::generator` é relançada no ponto em
  que o consumidor pediu o próximo item (`++it`, ou o `for` de range).
- `std::string_view` produzido por `lines()` só precisa valer até o próximo
  item; um `Record` que vai para um `std::vector` precisa valer para sempre.
- `records(src) | std::views::filter(...) | std::views::take(n)` é um range
  como qualquer outro, e `std::ranges::to<std::vector>()` o materializa.
- `for (x : walk_dir(sub)) co_yield x;` faz cada item subir um nível por vez.
  Leia sobre `std::ranges::elements_of` na página de `std::generator`.
- Em `walk_dir`, que parâmetros são copiados quando a corrotina é criada?

## Nível 3

Abordagem, função por função:

1. `lines()`: abra a fonte e crie logo depois um pequeno objeto local cujo
   destrutor chama `close()`; remova o `close()` manual. Leia em blocos e, a
   cada bloco, produza todas as linhas completas antes de ler o próximo; guarde
   só o pedaço incompleto entre leituras. Remova o `try/catch`.
2. `Record`: troque os campos `std::string_view` por `std::string`, e faça
   `parse_record` copiar o texto de cada campo.
3. `records()`: produza `parse_record(...)` diretamente, sem `try/catch`.
4. `first_matching()`: componha `records`, `views::filter`, `views::take` e
   `ranges::to<std::vector>`, sem materializar tudo antes.
5. `walk`: receba `const Dir&` e, para cada subdiretório, use
   `co_yield std::ranges::elements_of(walk_dir(...))`.

## Nível 4

Se `walk_profundo` ou `walk_custo_por_item` ainda falham, verifique se não
sobrou nenhuma cópia de `Dir` (laço `for (Dir sub : ...)` sem `&`, parâmetro
por valor) e se nenhum nível re-produz itens do filho com um `for`. Se
`lines_fecha_ao_parar` falha com `closes == 0`, o `close()` ainda está depois
do laço, e não num destrutor.
