# Objetivos de aprendizagem — desafio-11

Ao concluir este desafio você será capaz de:

1. **Medir cópias escondidas** e localizar o ponto em que um suposto movimento vira cópia.
2. **Reconhecer a supressão do movimento implícito**: um destrutor (ou cópia) declarado
   pelo usuário impede o compilador de gerar as operações de movimento (regra do zero/cinco).
3. **Usar `noexcept` no movimento** para que `std::vector` (via `std::move_if_noexcept`)
   mova em vez de copiar ao realocar.
4. **Escrever interfaces que não forçam cópias**: parâmetros *sink* por valor + `std::move`,
   `emplace_back`, retorno por valor sem `std::move` (NRVO).
5. **Evitar armadilhas de `std::move`**: mover um objeto `const` copia em silêncio; um
   objeto movido precisa ficar válido, inclusive em `a = std::move(a)`.
6. **Preservar a semântica de valor**: cópia explícita continua profunda e independente —
   economizar cópias compartilhando o payload não é aceito.

## Como os testes verificam cada objetivo

As cópias profundas são medidas contando, num `operator new` substituído pelos testes,
os blocos alocados com o tamanho exato de um payload (100003 bytes). Todos os testes rodam
com ASan/UBSan/LSan, um processo por teste.

| Objetivo | Verificação |
|---|---|
| 1. Medir cópias | Cada teste de `Batch`/`Message` informa quantas cópias profundas foram feitas contra um orçamento (em geral 0); `pipeline_mil_mensagens` exige exatamente 1000 alocações de payload para 1000 mensagens em `add → transform → dispatch`. |
| 2. Movimento suprimido | `message_move`: `Message b(std::move(a))` e `c = std::move(b)` com 0 cópias e origem vazia; `message_noexcept` e o critério `static_nothrow_move` (compilado à parte com `static_assert`). |
| 3. `noexcept` | `buffer_vector_realocacao`: 64 `push_back` sem `reserve` com 0 cópias extras; `buffer_noexcept`, `message_noexcept` e `static_nothrow_move` checam `std::is_nothrow_move_constructible/assignable`. |
| 4. Sink, `emplace_back`, NRVO | `message_construcao_sem_copia` (temporário/movido: 0 cópias; lvalue: exatamente 1), `batch_add_sem_copia` (300 `add` com 0 cópias extras), `message_compose_sem_movimento` (`LifecycleProbe`: 0 cópias e 0 movimentos de `Message`). |
| 5. `const` e estado pós-movimento | `batch_dispatch_sem_copia`, `batch_transform_sem_copia` e `batch_drain_sem_copia` (0 cópias, ordem e conteúdo preservados, lote vazio depois); `buffer_move_atribuicao` e `buffer_auto_movimento` (ASan: sem double free/uso após liberação; origem vazia e reutilizável). |
| 6. Semântica de valor | `message_copia_independente` e `buffer_copia_profunda`: cópia explícita faz exatamente 1 alocação de payload e alterar um lado não altera o outro; `batch_add_sem_copia` exige 1 cópia ao adicionar um lvalue. Compartilhar o payload (`shared_ptr`) reprova. |
