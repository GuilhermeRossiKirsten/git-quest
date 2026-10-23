# Objetivos de aprendizagem — desafio-28

Ao concluir este desafio você será capaz de:

1. **Reconhecer o custo do despacho por tipo espalhado**: `enum` + `switch` + `static_cast`
   (e `dynamic_cast` em cascata) obriga a editar e recompilar o núcleo a cada tipo novo, e cada
   lugar esquecido vira um bug silencioso (princípio aberto/fechado).
2. **Diagnosticar fatiamento** e a "cópia que perde o tipo" em hierarquias copiadas por valor
   (`std::vector<Base>`, `make_unique<Base>(derivado)`), usando o UBSan (`-fsanitize=vptr`).
3. **Implementar Type Erasure**: um tipo valor (`promo::Promotion`) que aceita qualquer regra sem
   herança, combinando External Polymorphism (Concept/Model), Bridge (ponteiro para a
   implementação) e Prototype (`clone()` para copiar).
4. **Escrever o contrato com um concept C++20** (`promo::PromotionRule`) e restringir o
   construtor, para que tipos inválidos sejam rejeitados em tempo de compilação com uma mensagem
   que cita o contrato.
5. **Preservar a semântica de valor**: cópias independentes, movimento `noexcept` sem alocação,
   regra do zero no `Checkout`, nenhum vazamento.
6. **Comparar as alternativas**: hierarquia virtual (tipos abertos, mas semântica de referência e
   fatiamento), `std::variant` (operações abertas, tipos fechados) e Type Erasure (tipos abertos,
   valores).

## Como os testes verificam cada objetivo

Todos os testes rodam com ASan/UBSan/LSan, um processo por teste. Os de alocação substituem o
`operator new` global (`tests/support/alloc_counter.cpp`).

| Objetivo | Verificação |
|---|---|
| 1. Despacho por tipo | Seção 1 (`ref_*`): total, cupom e JSON de referência das quatro promoções — `ref_frete_gratis`, `ref_caixa_completo` e `ref_funcoes_livres` reprovam o `{"type":"unknown"}` da cascata de `dynamic_cast`. Seção 2: `tests/plugin_acme.cpp` e `tests/plugin_tracked.cpp`, unidades de tradução separadas, definem tipos novos e precisam compilar e funcionar sem que `src/` os conheça (`plugin_promocao_direta`, `plugin_caixa_misto`, `plugin_regra_grande`). |
| 2. Fatiamento | Seção 3: `valor_sem_fatiamento` (`Promotion p = BuyXPayY(...)`), `valor_vetor_heterogeneo`, `valor_promocoes_snapshot` e `valor_copia_do_caixa` — no código inicial o UBSan acusa "does not point to an object of type 'BuyXPayY'". |
| 3. Type Erasure | Seção 2 inteira (regras comuns, sem herança, inclusive uma de 512 bytes) e `valor_plugin_copias` (cópias de caixas e vetores com regras do plugin). Uma hierarquia virtual ou um `std::variant` não compila os plugins. |
| 4. Concept | Seção 5: `tests/contracts/01..06` usam concept-sondas (`tests/support/probes.hpp`) — uma regra válida é aceita por `PromotionRule`, pela construção de `Promotion` e por `Checkout::add`; regras sem `label`/`to_json`/`discount`, com `discount` devolvendo `double` ou não-`const`, só-movíveis, e também `int`, `std::string`, `Cart` e ponteiros são rejeitados. `tests/diagnostics/regra_sem_label.cpp` exige que o erro cite `PromotionRule`. |
| 5. Semântica de valor e memória | `valor_copia_independente` (cópia, reatribuição, auto-atribuição, objeto movido reutilizado, cópia que sobrevive ao original), `valor_plugin_vida_util` (toda cópia da regra destruída exatamente uma vez); seção 4: `mem_mover_sem_alocar` (0 alocações), `mem_construir_e_copiar` e `mem_plugin_alocacoes` (<= 2), `mem_vetor_crescendo` (100 `push_back` <= 150 alocações: realocar não copia), `mem_total_sem_alocar` (0); contrato `07_promotion_e_valor` (copiável, movimento `noexcept`); LSan em todos. |
| 6. Alternativas | As soluções erradas típicas reprovam por motivos diferentes: só `dynamic_cast` ou hierarquia virtual → plugins não compilam e há fatiamento; `std::variant` → plugins não compilam; construtor sem concept → contratos; movimento que copia → seção 4. |
