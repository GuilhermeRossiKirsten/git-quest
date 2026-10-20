# Solução de problemas

Comece sempre por `./scripts/check-environment.sh`.

## Docker

**`O daemon do Docker não está respondendo`**
Inicie o Docker Desktop (Windows/macOS) ou `sudo systemctl start docker` (Linux). No
Linux, `permission denied ... docker.sock` significa que seu usuário não está no grupo
`docker`: `sudo usermod -aG docker $USER` e faça logout/login.

**`Falha ao construir gitquest/...`**
O log resumido aparece acima da mensagem. Causas comuns: sem internet para baixar a
imagem base na primeira vez; disco cheio (`docker system df`, `docker image prune`).

**Arquivos em `build/` pertencem ao root (Linux)**
Acontece se você executou o container manualmente como root. Apague com
`docker run --rm -v "$PWD":/w busybox:1.36 rm -rf /w/build` e rode de novo.

**Lento no Windows**
Clone dentro do WSL (`~/gitquest`) em vez de `/mnt/c/...`.

## Scripts

**`/usr/bin/env: 'bash\r': No such file or directory`**
O arquivo foi convertido para CRLF. O `.gitattributes` força LF; restaure com
`git rm --cached -r . && git reset --hard` (descarta alterações não commitadas!) ou
configure `git config core.autocrlf input` e faça checkout novamente.

**`mapfile: command not found` / `declare: -A: invalid option` (macOS)**
O bash do sistema é 3.2. `brew install bash` e coloque-o antes no `PATH`.

**`Python >= 3.8 não encontrado`**
Instale `python3`. No Windows/Git Bash, o executável pode se chamar `python` — ambos
são detectados.

**PowerShell/cmd: "não é reconhecido como comando"**
Os scripts são Bash. Use WSL2 ou Git Bash.

## Git

**`gitquest start` diz que há modificações locais**
Proposital — o GitQuest nunca descarta trabalho. Faça commit (`git add -A && git commit -m wip`)
ou `git stash push -m gitquest`, e repita o comando.

**`Catálogo não encontrado` numa branch de desafio**
A CLI lê o catálogo da `main`. Rode `git fetch origin main`.

**`desafio-N não existe`**
`git fetch origin` para trazer as branches remotas; confira o id com `gitquest list`.

## Kubernetes (Kind)

**`cluster gitquest não encontrado`**: rode `./start.sh` no desafio.
**Cluster lento ou sem memória**: aumente a memória do Docker Desktop (≥ 4 GB).
**Remover tudo**: `kind delete cluster --name gitquest` (ou `./.tools/bin/kind ...`).

## Testes

**Passa localmente, falha na CI (ou vice-versa)**
Os testes rodam no mesmo container nos dois lugares; diferenças costumam ser de tempo.
Rode `./test.sh` várias vezes — se oscilar, há uma condição de corrida no seu código
(é exatamente o que o race detector e os sanitizers procuram).

**"os testes passaram com o código inicial"** (`validate-branch.sh`)
Mensagem para autores: os testes não detectam o problema plantado. Fortaleça-os.
