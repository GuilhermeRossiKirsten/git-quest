# desafio-24 · O cache que mentia

> Trilha: database · Nível: advanced · ~75 min · 250 pontos · Go 1.25.3 + Redis 7.4.2 (Docker Compose)

## Contexto

O `catalog` é o serviço que entrega nome e preço dos produtos para as lojas
do marketplace. O banco de produtos é lento e caro, então alguém colocou um
Redis na frente: o serviço procura o produto no cache e, se não acha, busca
no banco e guarda o resultado (*cache-aside*). Seis réplicas do serviço
dividem o mesmo Redis.

Funcionou bem — até a semana da Black Friday. O time comercial baixou o preço
da furadeira e o site continuou mostrando o preço antigo por dez minutos. A
loja `globex` reclamou que seu produto 42 aparecia com o nome do produto 42 de
outra loja. Quando a vitrine abriu, o banco recebeu centenas de consultas
idênticas no mesmo segundo; dez minutos depois, de novo. Um robô varrendo IDs
inexistentes derrubou a latência de todo mundo. E, quando o Redis travou por
alguns minutos, o catálogo inteiro travou junto — mesmo com o banco saudável.

## Problema

Rode `./start.sh`: ele sobe o Redis, compila o serviço e executa
`cmd/demo`, que reproduz seis situações de produção contra o Redis real e
mostra o que o serviço faz. Sintomas observáveis:

- Depois de uma troca de preço concluída com sucesso, as duas réplicas
  continuam lendo o preço antigo.
- Lojas diferentes com o mesmo ID de produto recebem o produto uma da outra.
- 100 acessos simultâneos a um produto que não está no cache viram 100
  consultas ao banco.
- Consultas repetidas a IDs que não existem vão todas ao banco.
- Produtos carregados juntos expiram todos no mesmo segundo.
- Com o Redis travado, uma leitura espera até o prazo do cliente e falha,
  embora o banco esteja respondendo.

Investigue `internal/catalog/` e faça o serviço cumprir o contrato abaixo —
corrigindo o código existente, não trocando o Redis por outra coisa.

**Contrato do serviço**

1. `GetProduct(ctx, loja, id)` devolve o produto daquela loja ou um erro que
   satisfaz `errors.Is(err, productdb.ErrNotFound)`. Lojas e IDs são texto
   livre (podem conter `:`, espaços, `{}`); pares (loja, id) diferentes são
   produtos diferentes.
2. O cache é o Redis, compartilhado pelas réplicas: leituras repetidas — de
   qualquer réplica — não voltam ao banco.
3. Quando `SaveProduct` retorna sem erro, **toda** leitura iniciada depois,
   em qualquer réplica, vê o produto novo — mesmo que outras leituras do mesmo
   produto estivessem em andamento durante a gravação, por mais lentas que
   fossem. `SaveProduct` não pode esperar leituras em andamento terminarem.
4. Leituras simultâneas do mesmo produto fora do cache geram **uma** consulta
   ao banco por réplica; leituras de outros produtos não esperam por elas.
5. Produtos ficam no cache entre 5 e 15 minutos, e as expirações de um lote
   carregado de uma vez se espalham por pelo menos 2 minutos.
6. "Não existe" também é cacheado, por no máximo 60 segundos; um produto
   criado com `SaveProduct` aparece imediatamente.
7. Se o Redis trava ou cai, leituras são servidas pelo banco e gravações
   concluem no banco, cada chamada em até 1 segundo.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Ordenar escrita e invalidação no cache-aside e explicar por que invalidar *antes* de gravar deixa valor velho no cache.
- Impedir que uma leitura lenta repovoe o cache com o valor antigo (a corrida do *stale set*), com leases ou gravação condicional.
- Evitar o *cache stampede* (*thundering herd*) coalescendo leituras concorrentes, só com a biblioteca padrão.
- Espalhar expirações com TTL + *jitter* e proteger o banco com cache negativo (*cache penetration*).
- Projetar chaves sem colisão: namespace, versão de formato, tenant e escape.
- Tratar o cache como otimização: prazos curtos e degradação para o banco.

## Conhecimentos necessários

- Go: `context` (prazos com `context.WithTimeout`), goroutines, `sync.Mutex`, canais, `encoding/json`, `errors.Is`.
- Redis: `GET`, `SET ... PX`, `DEL`, `PTTL`, `EVAL` (scripts Lua executam de forma atômica).
- Cache-aside e consistência eventual; o desafio-23 (PostgreSQL, consultas e índices) dá o contexto do "banco lento".

## Ambiente

- Docker >= 20.10 com **Docker Compose v2 >= 2.20** (`docker compose version`).
- Bash (Linux, macOS ou Windows via WSL2/Git Bash).
- `docker-compose.yml` sobe só o `redis:7.4.2-alpine3.21` (sem persistência,
  `maxmemory-policy noeviction`), com healthcheck, na rede **interna**
  `gitquest-desafio-24-net`. A porta do Redis **não** é exposta no host.
- O código Go roda em containers descartáveis da imagem de toolchain
  (`Dockerfile`, `golang:1.25.3-bookworm`), como usuário não-root, ligados só
  à rede interna (sem internet). Só biblioteca padrão: o cliente Redis é o
  pacote `internal/redisx` (protocolo RESP, ~300 linhas) e o "banco" é
  `internal/productdb`, um banco simulado em memória com latência e
  contadores de consultas.
- Cache de build no volume `gitquest-desafio-24-gocache`.

## Como começar

```bash
./start.sh            # idempotente: imagem, Redis, compilação e demonstração
./start.sh --down     # ao terminar: derruba o Redis e apaga o cache de build
```

Ferramentas de diagnóstico:

- A saída de `cmd/demo` (rode `./start.sh` de novo depois de cada mudança).
- `redis-cli` dentro do container (a demonstração usa o banco lógico 1 e
  deixa nele as chaves do último cenário):
  `docker compose -p gitquest-desafio-24 exec redis redis-cli -n 1` — experimente
  `--scan`, `GET <chave>`, `PTTL <chave>` e, em outro terminal, `MONITOR`
  enquanto roda `./start.sh` para ver cada comando que o serviço envia.
- `./test.sh` e o log completo em `build/test.log`: cada teste explica o que
  viu (quantas consultas ao banco, qual preço cada réplica leu, quais TTLs).
- Leia `internal/productdb` (como os testes travam uma leitura ou gravação no
  meio para forçar uma intercalação) e `internal/redisx` (o que acontece com
  um comando quando o `ctx` não tem prazo).

## Arquivos que você pode modificar

- `internal/catalog/` — o serviço (`service.go`) e as chaves (`keys.go`).
  Mantenha a API pública (`New`, `Service.GetProduct`, `Service.SaveProduct`,
  `Store`); pode criar arquivos no pacote.

Não altere `internal/redisx`, `internal/productdb`, `cmd/`, `tests/`, `env/`,
`test.sh`, `start.sh`, `docker-compose.yml`, `Dockerfile`, `go.mod`,
`challenge.yaml` nem o `Makefile`: o avaliador usa as versões originais. Não
há dependências externas disponíveis (os testes rodam sem internet) — e nem
são necessárias.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

Sobe o Redis se preciso, roda `go vet ./...` e `go test -race -count=1
./tests/...` num container na rede do compose e imprime ✔/✘ por critério,
com o motivo. Cada teste esvazia o banco lógico 0 do Redis, cria seu próprio
banco simulado e duas réplicas do serviço. Leva cerca de 20 s.

## Critérios de conclusão

- [ ] o cache é usado e compartilhado: leituras repetidas, de qualquer réplica, não voltam ao banco
- [ ] lojas diferentes com o mesmo ID (e IDs com `:`, espaços e `{}`) nunca recebem o produto uma da outra
- [ ] depois de `SaveProduct`, todas as réplicas leem o valor novo — mesmo com uma leitura durante a gravação
- [ ] uma leitura lenta iniciada antes da gravação não repovoa o cache com o valor antigo
- [ ] 50 leituras simultâneas de um produto fora do cache geram 1 consulta ao banco, sem bloquear outros produtos
- [ ] TTL de produtos entre 5 e 15 min, com no máximo 5% das expirações no mesmo segundo e janela ≥ 100 s
- [ ] IDs inexistentes: no máximo 1 consulta ao banco por ID; a entrada negativa expira em ≤ 60 s
- [ ] Redis travado ou fora do ar: leituras servidas do banco e gravações concluídas em ≤ 1 s
- [ ] `go test -race` sem data races e `go vet` sem alertas

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- Rajesh Nishtala et al., "Scaling Memcache at Facebook", NSDI 2013 — seção 3.2.1 (*Leases*: *stale sets* e *thundering herds*): https://www.usenix.org/conference/nsdi13/technical-sessions/presentation/nishtala
- *Continuous Architecture in Practice*, Murat Erder, Pierre Pureur e Eoin Woods — cap. 5, "Scalability as an Architectural Concern" (seção "Caching for Scalability": invalidação e seus custos) e cap. 7, "Resilience as an Architectural Concern" (seção "Defaults and Caches": *lookaside caches* e o que fazer quando uma dependência falha).
- *Exam Ref AZ-204 Developing Solutions for Microsoft Azure*, 3ª ed., Santiago Fernández Muñoz — cap. 4, Skill 4.1 "Implement caching for solutions" (padrão cache-aside, TTL e `EXPIRE` no Redis).
- *Sistemas Distribuídos: Princípios e Paradigmas*, 2ª ed., Andrew S. Tanenbaum e Maarten van Steen — cap. 7, "Consistência e replicação" (7.3 modelos centrados no cliente, como *read-your-writes*; 7.4–7.5 réplicas e protocolos de invalidação — um cache é uma réplica).
- *Designing Data-Intensive Applications*, Martin Kleppmann — cap. 5 (*reading your own writes*) e cap. 11–12 (caches como dados derivados e o problema da escrita dupla). *(citado de memória)*
- Microsoft Azure Architecture Center — "Cache-Aside pattern": https://learn.microsoft.com/azure/architecture/patterns/cache-aside
- Documentação do Redis — `EXPIRE` (como as chaves expiram) e `SET` (opções `PX`/`NX`): https://redis.io/docs/latest/commands/expire/ · https://redis.io/docs/latest/commands/set/
- Documentação do Redis — scripts Lua com `EVAL` (atomicidade): https://redis.io/docs/latest/develop/interact/programmability/eval-intro/
- Documentação do Redis — *hash tags* no Redis Cluster: https://redis.io/docs/latest/operate/oss_and_stack/reference/cluster-spec/#hash-tags
- `golang.org/x/sync/singleflight` (a ideia; aqui você a implementa com a stdlib): https://pkg.go.dev/golang.org/x/sync/singleflight

## Próximo desafio

**desafio-25 · Quem pode o quê** (trilha kubernetes): RBAC de menor privilégio
e NetworkPolicy *deny-by-default*. Para ir além neste tema: troque a
invalidação síncrona por *change data capture* do banco, e compare leases com
*stale-while-revalidate* e *probabilistic early expiration*.
