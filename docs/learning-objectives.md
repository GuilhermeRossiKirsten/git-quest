# Objetivos de aprendizagem — desafio-24

Ao concluir este desafio você será capaz de:

1. Implementar **cache-aside** corretamente: gravar na fonte da verdade e só
   depois invalidar o cache, explicando por que invalidar **antes** de gravar
   deixa uma leitura concorrente repovoar o cache com o valor antigo.
2. Reconhecer e impedir o **stale set**: uma leitura lenta que buscou o valor
   antes de uma gravação e o grava no cache depois da invalidação — com
   **leases** (tokens de preenchimento revogados pela gravação) ou gravação
   condicional por **versão**, num script Lua atômico.
3. Evitar o **cache stampede** (*thundering herd*) coalescendo leituras
   concorrentes da mesma chave (*singleflight* com a biblioteca padrão), sem
   serializar chaves diferentes.
4. Usar **TTL com jitter** para que entradas carregadas juntas não expirem
   juntas, e **cache negativo** com TTL curto contra *cache penetration*.
5. Projetar **chaves** sem colisão: namespace, versão do formato, tenant e
   escape de texto livre (e *hash tags* quando duas chaves precisam do mesmo
   slot no Redis Cluster).
6. Tratar o cache como **otimização**: prazos curtos por comando e
   degradação para o banco quando o Redis trava ou cai.

## Como os testes verificam cada objetivo

Todos os testes rodam contra o Redis 7.4 real (banco lógico 0, esvaziado a
cada teste) com duas réplicas do serviço (dois clientes Redis) e o banco
simulado `internal/productdb`, que conta as consultas e permite travar uma
leitura ou gravação no meio para forçar uma intercalação exata.

| Objetivo | Verificação (`tests/`) |
|---|---|
| 1. Ordem escrita/invalidação | `TestInvalidacao_LeituraDuranteEscrita`: com `HoldNextSave`, uma leitura (da outra réplica) acontece enquanto a gravação está presa antes de alterar o banco; depois que `SaveProduct` retorna, as duas réplicas têm de ler o preço novo. Também cobre o caso sem concorrência. |
| 2. Stale set | `TestInvalidacao_CorridaLeituraLenta`: com `HoldNextGet`, uma leitura com cache vazio já pegou o preço antigo no banco e fica presa; a outra réplica grava o preço novo (sem poder esperar a leitura); a leitura é liberada; as duas réplicas têm de ler o preço novo. *Delete* depois da gravação sozinho, ou *delete* duplo atrasado, falham. |
| 3. Stampede | `TestStampede_UmaIdaAoBanco`: 50 leituras simultâneas de um produto fora do cache, com a primeira consulta presa no banco; exige exatamente 1 consulta ao banco e todas as respostas corretas. Enquanto isso, a leitura de outro produto tem de responder em até 2 s (um mutex global falha). |
| 4. TTL, jitter e cache negativo | `TestTTL_ComJitter`: 300 produtos carregados em sequência; lê `PTTL` de todas as chaves: nenhuma sem expiração, produtos entre 5 e 15 min, no máximo 5% expirando no mesmo segundo e janela ≥ 100 s. `TestNegativo_IdInexistente`: 20 IDs inexistentes lidos 6 vezes (2 réplicas) → no máximo 20 consultas ao banco, todas as chaves com TTL ≤ 60 s, `ErrNotFound` preservado e um produto criado aparece imediatamente nas duas réplicas. |
| 5. Chaves | `TestChaves_SemColisao`: lojas `acme`/`globex` com o mesmo ID, os pares `("loja:sp","7")`, `("loja","sp:7")`, `("loja:sp:7","")` e um ID com espaço e `{}`; lidos do banco, do cache e pela outra réplica, em ordens diferentes. |
| 6. Degradação | `TestRedisFora_Degrada`: um servidor TCP que aceita e nunca responde (Redis travado) e uma porta fechada (Redis fora); leituras devolvem o produto do banco em ≤ 1 s (com prazo do chamador de 3 s) e `SaveProduct` grava no banco em ≤ 1 s. |
| Base | `TestCache_CompartilhadoEntreReplicas`: 10 leituras (5 por réplica) = 1 consulta ao banco e a entrada está no Redis; `ErrNotFound` para ID inexistente. Mais `go test -race` e `go vet ./...`. |
