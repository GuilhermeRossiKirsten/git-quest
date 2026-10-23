# desafio-29 · Metros mais segundos

> Trilha: cpp · Nível: advanced · ~75 min · 250 pontos · C++20 (`-std=c++20 -pedantic-errors`, g++ 14.2)

## Contexto

Em 1999, a sonda *Mars Climate Orbiter* foi perdida porque um programa de solo
entregava impulso em libra-força·segundo e outro o lia como newton·segundo.
Nenhum compilador reclamou: para ele, tudo era `double`.

A equipe de navegação desta missão jurou que isso não aconteceria de novo e
escreveu a `units`, uma biblioteca header-only de grandezas físicas em que a
dimensão (comprimento, massa, tempo) faz parte do **tipo**. A promessa era:
"se não faz sentido físico, não compila — e custa o mesmo que um `double`".

O relatório de telemetria de hoje mostra que a promessa não foi cumprida:

```text
impulso total                9.0000 N·s
delta-v                    0.026627 m/s
desvio em 90 dias             207.1 km
```

A navegação esperava um impulso perto de **40 N·s** e um desvio de uns **921 km**.
Pior: um estagiário descobriu que `10.0_m + 2.0_s` compila sem nenhum aviso.

## Problema

Sintomas observáveis (rode `./start.sh` e `./test.sh`):

- `10.0_m + 2.0_s`, `1.0_m == 1.0_s`, `Meters d = 5.0;` e `double x = 3.0_m;` compilam;
- uma velocidade (`10.0_m / 2.0_s`) pode ser guardada numa variável `Meters`, e `2.0_kg * 3.0_m` "vira" `Newtons` sem reclamar;
- `std::cout << 100.0_m / 20.0_s` imprime `5 m s` em vez de `5 m s^-1`, e `2.0_m * 3.0_m` imprime `6 m`;
- `1.0_km + 500.0_m` vale 501000 m, mas `500.0_m + 1.0_km` vale 501 m;
- `10.0_s` vale 10 s, mas `10_s` vale 0,01 s;
- nada disso pode ser usado em `constexpr` (`static_assert((1.5_km).value() == 1500.0)` não compila);
- `sizeof(Meters)` é o dobro de `sizeof(double)` e `Meters` não é trivialmente copiável;
- o relatório de telemetria (`build/telemetry`) mostra um impulso total ~4,4× menor que o esperado.

### API pública (os testes usam exatamente estes nomes)

Tudo no namespace `units`, no cabeçalho `src/units/units.hpp`:

| Nome | O que é |
|---|---|
| `Dim<L, M, T>` | dimensão: expoentes inteiros (parâmetros não-tipo) de comprimento, massa e tempo, expostos como `Dim::length`, `Dim::mass`, `Dim::time` |
| `Dimensionless`, `Length`, `Mass`, `Time`, `Area`, `Frequency`, `Velocity`, `Acceleration`, `Force`, `Impulse`, `Energy` | aliases de `Dim` |
| `Quantity<D, Rep = double>` | valor `Rep` com dimensão `D`; membros `dimension`, `rep`, `value()` (valor na unidade SI base) |
| `Scalar`, `Meters`, `Kilograms`, `Seconds`, `SquareMeters`, `Hertz`, `MetersPerSecond`, `MetersPerSecondSquared`, `Newtons`, `NewtonSeconds`, `Joules` | aliases de `Quantity<…, double>` |
| `square(q)` | `q * q` |
| `newtons_per_pound_force` | 4.4482216152605 |
| `units::literals`: `_m`, `_km`, `_s`, `_kg`, `_lbf` | literais com ponto flutuante **e** inteiros (`1.5_km`, `3_km`, `10_s`…); todos produzem o valor já em SI (`1.5_km` é `Meters` valendo 1500) |

Regras que a biblioteca deve garantir:

| Operação | Regra |
|---|---|
| `Quantity{x}` / `Quantity{}` | construção a partir de `Rep` só **explícita**; padrão vale zero |
| `a + b`, `a - b`, `+=`, `-=`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `-a` | só entre grandezas da **mesma** dimensão e mesma `Rep` |
| `a * b`, `a / b` | dimensões se combinam: expoentes somam (`*`) ou subtraem (`/`) |
| `q * x`, `x * q`, `q / x`, `q *= x`, `q /= x` (`x` número puro) | escala, a dimensão não muda |
| `x / q` | inverte a dimensão (`1.0 / 2.0_s` é `Hertz`) |
| conversões | nenhuma conversão implícita de/para `Rep`, nem entre dimensões diferentes (nem `Meters{velocidade}`) |
| custo zero | `sizeof(Quantity<D, R>) == sizeof(R)`, trivialmente copiável, tudo utilizável em `constexpr` |
| `operator<<` | valor seguido das unidades com expoente: `5 m s^-1`, `6 m^2`, `6 m kg s^-2` |

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Codificar informação no tipo com parâmetros de template não-tipo (`Dim<1, 0, -1>`).
- Escrever *type functions* (metafunções) que calculam tipos a partir de tipos.
- Controlar conversões implícitas (`explicit`, construtores-template, operadores de conversão) e entender como elas furam a verificação de tipos.
- Garantir custo zero: tamanho, cópia trivial e avaliação em `constexpr`.
- Testar que código **não** compila, sem confundir "não compila pelo motivo certo" com "não compila".

## Conhecimentos necessários

- Templates de classe e de função, dedução de argumentos e concepts (desafio-4).
- Sobrecarga de operadores e literais definidos pelo usuário (`operator""`).
- Leitura de mensagens de erro do g++.

## Ambiente

- Docker >= 20.10; o compilador roda no container `gcc:14.2.0` (padrão **C++20**, `-std=c++20 -pedantic-errors`; testes de execução com AddressSanitizer e UndefinedBehaviorSanitizer).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- **Sem Docker:** com g++ >= 14 e `make` instalados, use `GITQUEST_LOCAL=1 ./start.sh` e `GITQUEST_LOCAL=1 ./test.sh`. No Windows (MinGW) os sanitizers são desligados; o resultado oficial é o do container.

## Como começar

```bash
./start.sh              # constrói a imagem, compila e mostra o relatório de telemetria
```

Ferramentas de diagnóstico úteis:

- `./test.sh` — cada critério aparece com ✔/✘ e o motivo (valor obtido, `static_assert` violado ou caso que compilou sem dever);
- compile um caso isolado para ver o erro completo, por exemplo:
  `docker run --rm -v "$PWD":/w -w /w gcc:14.2.0 g++ -std=c++20 -fsyntax-only -Isrc -Itests tests/contracts/06_custo_zero_tamanho.cpp`
  (para um arquivo de `tests/rejects/`, acrescente `-DGQ_VALID` para ver a forma que precisa compilar);
- descubra o tipo de uma expressão provocando um erro: `static_assert(std::is_same_v<decltype(10.0_m / 2.0_s), void>);` — a mensagem mostra o tipo real;
- `std::is_convertible_v`, `std::is_trivially_copyable_v` e `sizeof` em `static_assert` respondem perguntas sobre a classe sem executar nada.

## Arquivos que você pode modificar

- `src/units/units.hpp` — a biblioteca (é aqui que está a maior parte do trabalho);
- `src/mission/burns.hpp` e `src/telemetry.cpp` — o código de navegação; quando a biblioteca ficar rígida, o compilador vai apontar usos sem unidade nele, e eles precisam ser corrigidos (sem "calar" o compilador com casts que mudam o significado dos dados).

Não altere `tests/`, `test.sh`, `challenge.yaml` nem o `Makefile`: o avaliador usa as versões originais.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

A saída tem quatro blocos: compilação; valores em execução (um processo por
teste); contratos em tempo de compilação (`tests/contracts/`, cada arquivo é
uma lista de `static_assert`); e recusas (`tests/rejects/`): cada arquivo
**não** pode compilar, o erro tem que apontar a linha marcada com
`← não deve compilar`, e a mesma linha na forma válida (`-DGQ_VALID`) tem que
compilar — assim um erro de sintaxe qualquer não conta como recusa.

## Critérios de conclusão

- [ ] `tests/unit_tests.cpp`, `tests/mission_tests.cpp` e `src/telemetry.cpp` compilam com `-std=c++20 -pedantic-errors`, e o relatório executa sem erros.
- [ ] Todos os testes de valores passam sob ASan/UBSan (inclusive impulso ≈ 40,034 N·s e desvio ≈ 921 km).
- [ ] Os 9 arquivos de `tests/contracts/` compilam (dimensões, `constexpr`, custo zero, conversões).
- [ ] Os 14 casos de `tests/rejects/` são recusados na linha certa e suas formas válidas compilam.
- [ ] `./test.sh` termina com código 0.

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *The C++ Programming Language*, 4ª ed., Bjarne Stroustrup — §28.7 "SI Units Example" (28.7.1 Units, 28.7.2 Quantitys, 28.7.3 Unit Literals, 28.7.4 Utility Functions); §28.2 "Type Functions"; §19.2.6 "User-defined Literals" (tipos de parâmetro aceitos por `operator""`). Atenção: alguns literais de exemplo em 28.7.3 têm erros de escala — um bom exercício é achá-los.
- *Template Metaprogramming with C++*, Marius Bancila — cap. 2, "Non-type template parameters"; cap. 5, "Understanding and defining type traits" e "Querying type properties" (`is_trivial` × `is_trivially_copyable`); cap. 6, "Defining concepts", "Exploring requires expressions" e "Constraining non-template member functions".
- cppreference: [user-defined literals](https://en.cppreference.com/w/cpp/language/user_literal), [`explicit`](https://en.cppreference.com/w/cpp/language/explicit), [implicit conversions](https://en.cppreference.com/w/cpp/language/implicit_conversion), [TriviallyCopyable](https://en.cppreference.com/w/cpp/named_req/TriviallyCopyable), [default comparisons](https://en.cppreference.com/w/cpp/language/default_comparisons).
- *Mars Climate Orbiter Mishap Investigation Board — Phase I Report* (NASA, 1999).

## Próximo desafio

Este é, por enquanto, o último desafio da trilha de metaprogramação. Volte ao
`main` (`./scripts/gitquest list`) para escolher a próxima trilha.
