# Arquitetura

Este documento registra as decisões de projeto do GitQuest e os riscos aceitos.

## Objetivo

Um repositório em que cada fase de aprendizagem é uma branch Git autocontida, com
problema real, ambiente reproduzível e validação objetiva — sem um framework pesado.

## Visão geral

```text
                      ┌───────────────────────────── main ─────────────────────────────┐
                      │ config/challenge-catalog.yaml   lista de desafios e roteiro     │
                      │ config/shared-files.txt         o que é copiado p/ as branches  │
                      │ scripts/gitquest                CLI                             │
                      │ scripts/lib/gq.py               YAML-subset, schema, checksum   │
                      │ scripts/lib/common.sh           helpers Bash (Docker, mensagens)│
                      │ scripts/validate-branch.sh      contrato + teste esperado       │
                      │ scripts/create-challenge.sh     gera skeleton em branch órfã    │
                      │ scripts/sync-tooling.sh         propaga ferramentas às branches │
                      │ templates/challenge/            modelo de desafio               │
                      └───────────────┬────────────────────────────────────────────────┘
                     sync-tooling.sh  │ (cópia idêntica, verificada pela CI)
          ┌───────────────────────────┼───────────────────────────┐
   desafio-1 (órfã)            desafio-2 (órfã)      ...    desafio-N (órfã)
   README.md challenge.yaml    ...                          ...
   start.sh test.sh Makefile
   Dockerfile src/ tests/ docs/
   scripts/ (cópia)  .github/workflows/validate-challenge.yml (cópia)
```

## Decisões

### 1. Branches órfãs, uma por desafio

Cada `desafio-N` é criada com `git switch --orphan`, sem histórico em comum com a `main`
nem com outros desafios.

- **Por quê:** o requisito central é que cada branch contenha tudo do seu desafio e funcione
  de forma independente. Branches derivadas da `main` herdariam arquivos irrelevantes e
  gerariam conflitos (ex.: dois `README.md` com papéis diferentes) a cada atualização.
- **Consequência:** `git switch desafio-1` troca a árvore inteira; o participante vê só o
  desafio. Não há merges entre branches.

### 2. Ferramentas compartilhadas vendorizadas

A CLI, o validador e o workflow de CI precisam existir dentro de cada branch (o GitHub
Actions só executa workflows presentes na branch; o participante roda
`./scripts/gitquest test` dentro do desafio). Os arquivos listados em
`config/shared-files.txt` são **copiados** da `main` por `scripts/sync-tooling.sh`.

- A `main` é a única fonte de verdade; a CI de cada desafio falha se a cópia divergir.
- Alternativas descartadas: *submodule* (fricção para iniciantes, clone recursivo),
  *subtree* (histórico poluído), instalar a CLI globalmente (contraria "não instalar
  ferramentas globalmente").

### 3. Catálogo na main, metadados ricos na branch

`config/challenge-catalog.yaml` lista **todos** os desafios (inclusive os planejados) com
os dados necessários para navegar: título, trilha, nível, pontos, pré-requisitos e status.
O `challenge.yaml` de cada branch contém o contrato completo (objetivos, ambiente,
arquivos editáveis, checksum do código inicial). A CLI lê o catálogo da `main` local ou de
`origin/main` com `git show`, sem alterar a árvore de trabalho.

`list-challenges.sh --check` (executado na CI da `main`) garante que todo desafio
`available` tem branch e que toda branch está no catálogo.

### 4. Python só com biblioteca padrão e um subconjunto de YAML

O validador de schema precisa rodar em qualquer máquina sem `pip install`. `gq.py`
implementa um parser estrito de um **subconjunto** de YAML (mapas, listas, listas inline,
escalares). Recursos fora do subconjunto (âncoras, multilinha, mapas inline) são
**rejeitados com erro**, nunca interpretados errado. Todo arquivo aceito continua sendo
YAML válido para qualquer outra ferramenta.

### 5. Toolchains em containers com versão fixada

Cada desafio declara a imagem de toolchain (`gcc:14.2.0`, `golang:1.25.3-bookworm`,
`kindest/node:v1.32.2`). O validador rejeita `FROM` sem tag ou com `latest`.
A tag da imagem local é derivada do hash do `Dockerfile` (`gq_toolchain_image`), então
editar o Dockerfile nunca reaproveita uma imagem velha.

`gq_docker_run` executa o código do participante com: usuário não-root, `--network none`,
limites de CPU/memória/PIDs e `timeout` **dentro** do container (o mesmo comportamento em
Linux, macOS e Windows).

### 6. "O código inicial deve reprovar"

Cada `challenge.yaml` registra `starter_checksum`: o SHA-256 dos arquivos editáveis do
código inicial. `validate-branch.sh` decide o resultado esperado:

| Estado dos arquivos editáveis | Resultado exigido de `./test.sh` |
|---|---|
| iguais ao código inicial | **falhar** — prova que os testes detectam o problema |
| modificados (solução) | **passar** |

Assim a CI da branch canônica fica verde com o código inicial e a CI do fork do
participante fica verde só quando ele resolve o desafio.

### 7. Soluções de referência fora do repositório

As soluções vivem em um repositório **privado** separado (`gitquest-solutions/`, com a
mesma estrutura de caminhos dos arquivos editáveis). Nunca são commitadas neste
repositório, em nenhuma branch, nem em histórico. Os mantenedores verificam cada desafio
aplicando a solução sobre um worktree temporário (`verify-solutions.sh` no repositório
privado): o código inicial deve falhar e a solução deve passar.

### 8. Testes ocultos

Testes guardados na branch pública **não** são secretos. Quando um desafio precisar de
avaliação com testes ocultos (`validation.hidden_tests: true`), a avaliação acontece em um
executor separado que controla os testes: ele faz checkout da branch do participante,
copia apenas os arquivos de `editable` para uma árvore limpa do desafio, adiciona os
testes privados e roda em sandbox. Os desafios atuais não usam testes ocultos.

### 9. Progresso local e gamificação leve

`gitquest test` registra conclusões em `$(git rev-parse --git-common-dir)/gitquest/progress.tsv`:
fora do versionamento, compartilhado entre branches e worktrees. Pontos, conquistas
(`achievements` no `challenge.yaml`) e "Poliglota" (3+ trilhas) são derivados desse arquivo.
Não há servidor nem interface web.

## Contrato de um desafio

Detalhado em [challenge-format.md](challenge-format.md). Resumo:

- `challenge.yaml` válido no schema, `id` igual ao nome da branch;
- `README.md` com as 12 seções obrigatórias;
- `start.sh` (prepara o ambiente, idempotente) e `test.sh` (0 = todos os critérios);
- `docs/learning-objectives.md` e `docs/hints.md` com níveis sequenciais;
- arquivos `editable` explícitos — tudo o mais pertence ao avaliador.

## Riscos de manutenção e mitigação

| Risco | Mitigação |
|---|---|
| Ferramentas divergirem entre branches | cópia automatizada (`sync-tooling.sh`) + CI falha em divergência |
| Catálogo e branches dessincronizados | `list-challenges.sh --check` na CI da main |
| Imagens base mudarem por baixo | tags fixadas; em avaliação controlada, fixar por digest |
| Testes frágeis (tempo, concorrência) | limites generosos calibrados (≈10× a solução de referência), sementes fixas, `-count=1` |
| Solução vazar no histórico | soluções em repositório separado; o checksum detecta código inicial alterado |
| Muitos desafios para revisar | contrato único, template, validador e verificação automática das soluções |
| Recursos C++ recentes sem suporte | cada desafio verifica o compilador e os *feature-test macros* e falha com mensagem clara |
| Dependência de rede em tempo de teste | containers de teste rodam com `--network none`; dependências vêm na imagem |

## Escalando para centenas de desafios

Nada na arquitetura central depende do número de desafios: o catálogo é uma lista, cada
branch é autocontida e a CI roda por branch. Para crescer, adicione entradas ao catálogo,
crie branches com `create-challenge.sh` e mantenha `sync-tooling.sh` rodando após cada
mudança nas ferramentas.
