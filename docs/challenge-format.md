# Formato de um desafio

Todo desafio segue o mesmo contrato, verificado por `./scripts/gitquest validate`
(implementado em `scripts/lib/gq.py`).

## Estrutura da branch

```text
desafio-N/
├── README.md                    enunciado (12 seções obrigatórias)
├── challenge.yaml               metadados e contrato
├── start.sh                     prepara o ambiente (obrigatório, idempotente)
├── test.sh                      avaliador (obrigatório; 0 = todos os critérios)
├── Makefile                     alvos usados pelos scripts (quando fizer sentido)
├── Dockerfile                   toolchain com versão fixada (quando fizer sentido)
├── docker-compose.yml           serviços auxiliares (quando fizer sentido)
├── src/                         código inicial — normalmente o que o participante edita
├── tests/                       testes (do avaliador)
├── docs/
│   ├── learning-objectives.md   objetivos e como cada um é verificado
│   └── hints.md                 dicas progressivas
├── scripts/                     cópia das ferramentas da main (não editar)
└── .github/workflows/validate-challenge.yml   cópia (não editar)
```

Arquivos opcionais só existem quando o desafio precisa deles. Nenhum arquivo vazio
"para cumprir tabela".

## `challenge.yaml`

| Campo | Tipo | Obrigatório | Regras |
|---|---|---|---|
| `id` | string | sim | `desafio-N`, igual ao nome da branch |
| `title` | string | sim | igual ao catálogo |
| `summary` | string | sim | uma frase |
| `track` | string | sim | `cpp`, `dsa`, `go`, `docker`, `kubernetes`, `linux`, `network`, `database` |
| `language` | string | sim | linguagem principal (`c`, `cpp`, `go`, `sql`, `yaml`, `dockerfile`...) |
| `standard` | string | se C++ | `cpp11`, `cpp14`, `cpp17`, `cpp20`, `cpp23`, `cpp26` |
| `toolchain` | string | se C++ | imagem/compilador exato, ex.: `gcc:14.2.0` |
| `difficulty` | string | sim | `beginner`, `intermediate`, `advanced`, `expert` |
| `learning_objectives` | lista | sim | ≥ 1 |
| `environment.type` | string | sim | `docker`, `local`, `kind` |
| `environment.requires` | lista | sim | ferramentas do host, ex.: `[docker]` |
| `environment.start_command` | string | sim | normalmente `./start.sh` |
| `environment.test_command` | string | sim | normalmente `./test.sh` |
| `validation.type` | string | sim | `automated` |
| `validation.timeout_seconds` | inteiro | sim | 10–3600; a CI adiciona margem |
| `validation.hidden_tests` | booleano | não | ver [architecture.md](architecture.md#8-testes-ocultos) |
| `editable` | lista | sim | arquivos/pastas/globs que o participante pode alterar; nunca `tests/`, `test.sh`, `challenge.yaml` |
| `estimated_minutes` | inteiro | sim | 5–600 |
| `points` | inteiro | sim | 10–1000 |
| `tags` | lista | sim | ≥ 1 |
| `prerequisites` | lista | sim | ids de desafios (pode ser `[]`) |
| `next` | lista | não | desafios recomendados em seguida |
| `achievements` | lista | não | conquistas exibidas ao concluir |
| `starter_checksum` | string | não* | `sha256:<hex>` dos arquivos editáveis do código inicial |

\* Sem `starter_checksum` a CI não consegue exigir que o código inicial reprove; todo
desafio publicado deve tê-lo. Gere com `python3 scripts/lib/gq.py checksum .` depois de
`git add -A`.

Campos desconhecidos são erro (evita erros de digitação silenciosos).

### Subconjunto de YAML aceito

- mapas em bloco (`chave: valor`) e listas em bloco (`- item`), indentados com espaços;
- listas inline de escalares: `[a, b, "c d"]`, `[]`;
- escalares: strings (com ou sem aspas), inteiros, `true`/`false`, `null`;
- comentários em linha inteira (`# ...`) e ao fim de valores sem aspas (` # ...`).

Não suportado (erro explícito): âncoras/aliases, tags, strings multilinha (`|`, `>`),
mapas inline (`{a: 1}`), listas aninhadas inline, tabs na indentação, chaves duplicadas.

Exemplo:

```yaml
id: desafio-1
title: "O vazamento silencioso"
summary: "RAII e smart pointers num gerenciador de sessões legado que vaza memória."
track: cpp
language: cpp
standard: cpp11
toolchain: "gcc:14.2.0"
difficulty: beginner
learning_objectives:
  - "Identificar vazamentos com LeakSanitizer"
  - "Aplicar RAII com std::unique_ptr"
environment:
  type: docker
  requires: [docker]
  start_command: "./start.sh"
  test_command: "./test.sh"
validation:
  type: automated
  timeout_seconds: 300
editable:
  - src
estimated_minutes: 45
points: 100
tags: [memory, raii, smart-pointers]
prerequisites: []
next: [desafio-2]
achievements: ["Caçador de vazamentos"]
starter_checksum: "sha256:..."
```

## `README.md`

Seções `##` obrigatórias, nesta ordem:

1. `Contexto` — narrativa curta.
2. `Problema` — sintomas observáveis; **nunca** a solução.
3. `Objetivos de aprendizagem`
4. `Conhecimentos necessários`
5. `Ambiente` — requisitos e versões; padrão C++ exato quando aplicável.
6. `Como começar` — `./start.sh`.
7. `Arquivos que você pode modificar`
8. `Como testar` — `./test.sh` / `./scripts/gitquest test`.
9. `Critérios de conclusão` — lista verificável.
10. `Dicas` — como usar `gitquest hints`.
11. `Referências` — livros e documentação.
12. `Próximo desafio`

## `docs/hints.md`

Seções `## Nível 1`, `## Nível 2`, ... sequenciais. Nível 1 orienta a investigação com
uma pergunta; os seguintes apontam onde olhar e descrevem a abordagem. Nenhum nível
contém a solução pronta. `gitquest hints N` exibe os níveis 1..N.

## `start.sh`

- `#!/usr/bin/env bash` e `set -euo pipefail`;
- verifica pré-requisitos e versões (`gq_require_docker`, `gq_version_ge`);
- prepara o ambiente (imagens, clusters, serviços auxiliares) de forma **idempotente**;
- compila o código inicial quando aplicável;
- termina mostrando os próximos passos e o comando de teste;
- falha com mensagens compreensíveis (`gq_die "..."`).

## `test.sh`

- código de saída `0` somente quando todos os critérios obrigatórios passam;
- testa **comportamento** (execução, sanitizers, race detector, respostas HTTP, estado do
  cluster), não a presença de palavras no código;
- roda código do participante em container com limites e timeout (`gq_docker_run`);
- se o ambiente não estiver pronto, prepara-o ou explica como (`./start.sh`);
- relata cada critério com ✔/✘ e um motivo útil.

## Helpers disponíveis (`scripts/lib/common.sh`)

| Função | Uso |
|---|---|
| `gq_require_docker` | Docker instalado, daemon ativo, versão >= 20.10 |
| `gq_require_cmd cmd "como instalar"` | dependência obrigatória |
| `gq_version_ge have want` | comparação de versões |
| `gq_toolchain_image id Dockerfile` | tag derivada do conteúdo do Dockerfile |
| `gq_docker_build tag -│ctx [Dockerfile]` | build com log resumido em caso de erro |
| `gq_docker_ensure_image tag Dockerfile` | build apenas se faltar |
| `gq_docker_run image timeout cmd...` | execução isolada (não-root, sem rede, limites) |
| `gq_info/gq_ok/gq_warn/gq_die/gq_title` | mensagens padronizadas |
| `gq_banner_next_steps "./test.sh"` | rodapé do `start.sh` |

Variáveis: `GQ_CPUS`, `GQ_MEMORY`, `GQ_DOCKER_NETWORK` e o array `GQ_DOCKER_EXTRA`.
