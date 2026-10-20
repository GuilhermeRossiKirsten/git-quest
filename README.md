# desafio-9 · A imagem de um gigabyte

> Trilha: docker · Nível: intermediate · ~60 min · 200 pontos

## Contexto

O `greeter` é um microsserviço Go minúsculo: três rotas HTTP, nenhuma dependência
externa, umas 100 linhas de código. Mesmo assim, cada deploy leva minutos: o registry
reclama de espaço, o scanner de vulnerabilidades devolve centenas de alertas em pacotes
que o serviço nem usa, e o time de segurança abriu um chamado com o título
"o token de pagamentos está no registry?". O Dockerfile foi escrito às pressas no
primeiro dia do projeto e "funciona". Agora ele é seu.

## Problema

Rode `./start.sh` e observe:

- a imagem construída tem **cerca de 900 MB** para servir um binário de poucos MB;
- `docker history` mostra camadas com o compilador inteiro e uma cópia de **todo** o
  repositório — arquivos que nada têm a ver com a execução do serviço;
- `curl localhost:8080/whoami` revela com qual usuário o processo roda;
- o Docker não sabe dizer se o container está saudável (`docker ps` não mostra estado de saúde);
- em produção a plataforma roda containers com `--read-only --cap-drop ALL`, e o serviço
  não sobe nesse modo;
- qualquer mudança de uma linha num arquivo `.go` refaz o build inteiro, sem aproveitar cache.

O código do serviço (`app/`) está correto e **não deve ser alterado**. O problema é como
ele é empacotado.

Ferramentas de diagnóstico úteis: `docker history --no-trunc`, `docker image inspect`,
`docker run --rm -it --entrypoint sh <imagem>` (enquanto ainda houver shell…),
`docker build --progress=plain` (mostra `CACHED` por etapa), `docker inspect <container>`
(campo `State.Health`) e `docker save <imagem> | tar -t`.

## Objetivos de aprendizagem

Veja também [docs/learning-objectives.md](docs/learning-objectives.md).

- Separar build e runtime com *multi-stage build* e uma imagem final mínima.
- Gerar um binário Go estático, adequado a uma imagem sem libc.
- Executar como usuário não-root, com sistema de arquivos somente leitura e sem capabilities.
- Declarar um `HEALTHCHECK` que funcione numa imagem sem shell.
- Controlar o contexto de build com `.dockerignore` para não vazar segredos, `.git` e código-fonte.
- Ordenar instruções para aproveitar o cache de camadas do BuildKit.

## Conhecimentos necessários

- Instruções básicas de Dockerfile: `FROM`, `COPY`, `RUN`, `WORKDIR`, `CMD`/`ENTRYPOINT`, `USER`.
- Noção de camadas de imagem e do contexto de build (`docker build .`).
- Compilar um programa Go (`go build`); não é preciso saber escrever Go.

## Ambiente

- Docker >= 20.10 com BuildKit (`docker buildx`), Docker Desktop no Windows/macOS.
- Bash (Linux, macOS ou Windows via WSL2/Git Bash), Python >= 3.8 e, de preferência, `curl`.
- Imagens base fixadas: `golang:1.25.3-bookworm` e `gcr.io/distroless/static-debian12:nonroot`
  (o `start.sh` baixa as que faltarem). Você pode usar outra base de runtime, desde que com
  versão fixada (tag diferente de `latest` ou digest) — ou `scratch`.

O contexto de build é a **raiz deste desafio** (`docker build .`), onde também estão
`app/`, `docs/`, `.env`, `.git` e os scripts do avaliador.

## Como começar

```bash
./start.sh
docker run --rm -p 8080:8080 -e GREETING="Oi" gitquest/desafio-9-app:dev
curl localhost:8080/api/greeting
```

O binário do serviço aceita `serve` (padrão) e `healthcheck` — este último consulta
`http://127.0.0.1:8080/healthz` e termina com código 0 (saudável) ou 1.

## Arquivos que você pode modificar

- `Dockerfile` — como a imagem é construída.
- `.dockerignore` — o que entra no contexto de build.

Não altere `app/`, `.env`, `tests/`, `test.sh` nem `challenge.yaml`: o avaliador usa as
versões originais. O `.env` contém um token **falso** que simula uma credencial real.

## Como testar

```bash
./test.sh                    # ou: ./scripts/gitquest test
```

O avaliador constrói `gitquest/desafio-9-app:test` a partir do seu Dockerfile, sobe
containers de teste (publicados só em `127.0.0.1`), examina todas as camadas com
`docker save` e reconstrói uma cópia do contexto com um `.go` alterado para medir o cache.
Leva de 1 a 2 minutos; os containers de teste são sempre removidos.

## Critérios de conclusão

- [ ] 1. A imagem é construída a partir do `Dockerfile`.
- [ ] 2. A imagem final tem menos de 25 MB.
- [ ] 3. A imagem declara um `USER` não-root e o processo roda com uid ≠ 0 (`/whoami`).
- [ ] 4. A imagem declara `HEALTHCHECK` e o container fica `healthy` em até 45 s.
- [ ] 5. Nenhuma camada nem os metadados/histórico contêm o token do `.env`, arquivos de `.git`
      ou o código-fonte do serviço.
- [ ] 6. O serviço responde em `/healthz` e `/api/greeting`, lendo `GREETING` do ambiente.
- [ ] 7. O serviço funciona com `--read-only --cap-drop ALL --security-opt no-new-privileges`.
- [ ] 8. Alterar só um arquivo `.go` não invalida a etapa `RUN ... go mod download`
      (ela precisa existir e aparecer como `CACHED` no rebuild).

## Dicas

As dicas são progressivas — peça só o necessário:

```bash
./scripts/gitquest hints 1
```

## Referências

- *Kubernetes: Up & Running*, 3ª ed., Brendan Burns, Joe Beda, Kelsey Hightower e Lachlan
  Evenson — cap. 2 "Creating and Running Containers" (seções "Optimizing Image Sizes",
  "Image Security" e "Multistage Image Builds").
- *The Complete Developer*, Martin Krause — cap. 10 "Containerization with Docker" e
  cap. 11 "Setting Up the Docker Environment".
- Docker Docs — [Building best practices](https://docs.docker.com/build/building/best-practices/),
  [Multi-stage builds](https://docs.docker.com/build/building/multi-stage/),
  [.dockerignore](https://docs.docker.com/build/concepts/context/#dockerignore-files),
  [HEALTHCHECK](https://docs.docker.com/reference/dockerfile/#healthcheck) e
  [Build cache](https://docs.docker.com/build/cache/).
- [GoogleContainerTools/distroless](https://github.com/GoogleContainerTools/distroless).

## Próximo desafio

**desafio-10 — O Deployment que nunca ficou pronto**: a imagem que você acabou de
enxugar vai para um cluster Kubernetes (Kind), onde probes, recursos e Service precisam
estar certos para o Deployment ficar disponível.
