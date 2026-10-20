# Dicas — desafio-3

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Comece pelo primeiro relatório do AddressSanitizer: **onde** a memória lida foi liberada
(ou a qual quadro de pilha ela pertencia)? Quem era o dono daqueles bytes, e quanto tempo
esse dono viveu comparado à `std::string_view` que você devolveu?

Depois, para cada sintoma de valor errado, pergunte: como o código distingue "a chave não
existe" de "a chave existe e vale `-1`/`0`/vazio"? Ele *consegue* distinguir?

## Nível 2

- Em `src/config.cpp`, as funções públicas embrulham `lookup()` e as rotinas da seção
  "Rotinas herdadas". Leia a assinatura de `lookup()` e o comentário em `config.hpp`: o
  que ela devolve, por valor, quando a chave não existe?
- Uma `std::string_view` construída a partir de uma `std::string` local (ou de um
  `substr()` de `std::string`, que devolve *outra* `std::string`) morre junto com ela.
  Os únicos dados que vivem tanto quanto o `Config` estão em `values_`.
- `std::stol` e `std::atof` têm comportamentos documentados para entrada inválida,
  overflow e sobras no fim do texto — confira na cppreference e compare com a tabela
  "Especificação do formato" do README.
- O parser lê linha a linha com `std::getline`: o que sobra no fim da linha num arquivo
  CRLF? O que `trim()` considera "espaço"? Onde exatamente um comentário inline começa?
- `std::map::insert` não sobrescreve uma chave existente.

## Nível 3

Abordagem sugerida (uma correção por vez, rodando `./test.sh` a cada passo):

1. Faça `get()` procurar direto no mapa e devolver `std::nullopt` quando não achar, ou
   uma view **do valor guardado no mapa** quando achar. Faça as demais funções partirem
   do `std::optional` de `get()` em vez de um sentinela.
2. Troque `stol`/`atof` por `std::from_chars` (`<charconv>`): ele não lança, não depende
   de locale, devolve `std::errc::result_out_of_range` no overflow e informa até onde
   consumiu — exija que tenha consumido o texto todo.
3. `get_bool`: compare sem diferenciar maiúsculas contra as duas listas de palavras e
   devolva `std::nullopt` no resto. `typed()`: tente, nesta ordem, booleano (sem `1`/`0`),
   inteiro, ponto flutuante e, por último, texto — reutilizando os mesmos conversores.
4. No parser: `trim` com o conjunto `" \t\r"`, comentário inline só quando `#`/`;` vem
   depois de espaço/tab, e `insert_or_assign` para que a última ocorrência vença.
5. Em `entries()`, monte a chave sem prefixo a partir de uma `std::string_view` da chave
   guardada no mapa (`substr` de `string_view` é outra view, sem cópia), e não pule
   valores vazios.

## Nível 4

Se um caso continua falhando depois disso:

- `get_int("-0")` deve dar `0`, e `"007"` deve dar `7`; `"0x1F"`, `"1.5"` e `"4 2"`
  devem dar `std::nullopt`. Com `std::from_chars`, a regra "consumiu tudo?" cobre todos.
- `typed("100000000000000000000")` não cabe em `long` e por isso deve cair no `double`.
- `entries("")` só deve listar chaves sem `.`; `entries("s")` só chaves com prefixo `s.`.
- Lembre-se de que `std::string_view::data()` não é terminada em `'\0'`: não a passe para
  funções de C que esperam uma string terminada.
