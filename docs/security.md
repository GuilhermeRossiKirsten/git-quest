# Segurança e isolamento

Desafios executam, por definição, código incorreto: vazamentos, loops infinitos, acessos
inválidos de memória, containers defeituosos. As regras abaixo valem para todos os desafios.

## Execução local

- **Containers sem root**: `gq_docker_run` usa o UID/GID do usuário no Linux e `1000:1000`
  no Docker Desktop.
- **Sem rede** por padrão (`--network none`); dependências vêm embutidas na imagem de
  toolchain. Um desafio que precise de rede deve justificar e usar uma rede própria.
- **Limites**: `--cpus 2 --memory 2g --pids-limit 512` (ajustáveis por `GQ_CPUS`/`GQ_MEMORY`)
  e `timeout` dentro do container para todo comando.
- **`SYS_PTRACE`** é a única capability adicionada, necessária para o LeakSanitizer
  (ele usa ptrace para suspender threads). Ela vale apenas dentro do container do teste.
- **Escopo de arquivos**: o container monta somente o diretório do desafio em `/work`.
  Scripts nunca removem nada fora do próprio diretório (ex.: `build/`, `.tools/`).
- **Socket do Docker nunca é montado** em containers de desafio. Desafios de Docker e
  Kubernetes rodam os comandos `docker`/`kind`/`kubectl` no host, contra recursos com nomes
  prefixados (`gitquest/...`, cluster `gitquest`, namespace `desafio-N`) e sempre com
  `--context kind-gitquest` explícito — o contexto atual do kubeconfig nunca é usado.
- **Downloads** (ex.: `kind`, `kubectl`) têm versão fixada e SHA-256 verificado, e vão para
  `./.tools/bin` da branch, não para o sistema.

## Imagens

- Tags sempre fixadas; o validador rejeita `latest` ou ausência de tag.
- Em um ambiente de avaliação controlado, fixe por digest (`imagem@sha256:...`).
- Containers **não** são uma fronteira de segurança absoluta. Desafios avançados que
  exijam mais isolamento (ex.: exploits, kernel) devem rodar em VM ou executor dedicado.

## CI

- `validate-challenge.yml` usa apenas `pull_request` (nunca `pull_request_target`),
  `permissions: contents: read`, nenhum secret e `timeout-minutes` em todos os jobs.
- Código de PRs é tratado como não confiável: nada de credenciais, caches compartilhados
  graváveis ou deploys a partir desses jobs.

## Segredos

- Nenhuma credencial real no repositório. O validador procura padrões comuns (chaves AWS,
  chaves privadas, tokens do GitHub/Slack) em todos os arquivos versionados.
- Desafios que ensinam gestão de segredos usam valores **falsos** e explicitamente
  identificados (ex.: `gq_fake_...`).

## Relatando um problema

Abra uma issue sem detalhes exploráveis e marque como `security`; um mantenedor
entrará em contato.
