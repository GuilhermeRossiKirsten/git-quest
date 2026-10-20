# Primeiros passos

## 1. Requisitos

| Ferramenta | Versão mínima | Para quê |
|---|---|---|
| Git | 2.23 | `git switch`, branches dos desafios |
| Bash | 4.0 | CLI e scripts (`start.sh`, `test.sh`) |
| Python | 3.8 | validação de metadados (somente biblioteca padrão) |
| Docker | 20.10 | toolchains C++/Go, desafios de Docker e Kubernetes |

Não é preciso instalar compiladores, Go, Kind ou kubectl: os desafios usam imagens com
versões fixadas, e o desafio de Kubernetes baixa `kind`/`kubectl` fixados (com verificação
de SHA-256) para `./.tools/bin` dentro da própria branch quando eles não existem.

Verifique tudo de uma vez:

```bash
./scripts/check-environment.sh
```

## 2. Instalação por sistema

### Linux

```bash
sudo apt install git python3 make           # Debian/Ubuntu
# Docker Engine: https://docs.docker.com/engine/install/
sudo usermod -aG docker "$USER"             # rode docker sem sudo (faça logout/login)
```

### Windows (recomendado: WSL2)

1. `wsl --install -d Ubuntu` no PowerShell como administrador e reinicie.
2. Instale o Docker Desktop e habilite **Settings → Resources → WSL integration** para a distro.
3. Dentro do Ubuntu (WSL): `sudo apt install git python3 make`.
4. Clone o repositório **dentro do sistema de arquivos do Linux** (`~/gitquest`), não em
   `/mnt/c/...` — é muito mais rápido e evita problemas de permissão.

**Git Bash** também funciona para a maioria dos desafios (os containers são iniciados
pelo Docker Desktop). Limitações conhecidas: caminhos com espaços exigem aspas e
o PowerShell/cmd **não** executam os scripts — use sempre um terminal Bash.

Configure o Git para não converter finais de linha (o repositório já força LF via
`.gitattributes`, mas isso evita surpresas em arquivos novos):

```bash
git config --global core.autocrlf input
```

### macOS

```bash
brew install bash git python   # o bash do sistema é 3.2; o GitQuest exige >= 4
# Docker Desktop: https://docs.docker.com/desktop/install/mac-install/
```

Garanta que o `bash` do Homebrew vem antes no `PATH` (`which bash`).

## 3. Primeiro desafio

```bash
git clone <URL_DO_REPOSITORIO> gitquest && cd gitquest
./scripts/gitquest list
./scripts/gitquest start desafio-1
./start.sh
./scripts/gitquest test     # falha: o código inicial tem problemas — esse é o jogo
```

Leia `README.md` da branch, investigue o código em `src/`, corrija, rode os testes de novo.
Faça commits do seu progresso normalmente — a branch é sua:

```bash
git add -A && git commit -m "desafio-1: libera sessões no destrutor"
```

Para trabalhar em vários desafios em paralelo sem trocar de branch, use worktrees:

```bash
git worktree add ../gq-desafio-7 desafio-7
```

## 4. Publicando seu progresso

Faça um fork, e `git push origin desafio-1`. A CI do fork (`validate-challenge`) roda os
testes e só fica verde quando o desafio está resolvido.

## 5. Recomeçar um desafio

```bash
git switch desafio-1
git restore --source=origin/desafio-1 -- src   # volta só os arquivos editáveis
```

Isso descarta suas alterações nesses arquivos — faça um commit ou `git stash` antes se
quiser guardá-las.
