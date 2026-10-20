# Criando um desafio

Um bom desafio ensina **uma** coisa concreta, exige uma implementação real e tem uma
validação objetiva. Antes de escrever código, responda:

1. Qual conceito o participante precisa entender para resolver? (vira `learning_objectives`)
2. Qual sintoma observável o código inicial apresenta? (vira a seção `Problema`)
3. Como um teste **de comportamento** distingue a solução certa das erradas plausíveis?

## Passo a passo

### 1. Escolha o próximo número

```bash
./scripts/gitquest list          # veja os ids usados (inclusive os "planejados")
git fetch origin                 # garanta que conhece as branches remotas
```

Use o próximo número livre, ou um id `planned` do roteiro que você vai implementar.

### 2. Crie a branch a partir do template

```bash
./scripts/create-challenge.sh 33 \
  --title "Deadlock no worker pool" \
  --language cpp --standard cpp20 --difficulty advanced
```

O script valida os argumentos, recusa números de branches que já existem, cria a branch
**órfã** `desafio-33` num worktree separado (`../gitquest-desafio-33`), copia o template e
as ferramentas compartilhadas. Nunca sobrescreve arquivos sem `--force` e confirmação
interativa. Nada é commitado.

### 3. Defina os objetivos

Edite `challenge.yaml` (`learning_objectives`, `tags`, `editable`, `prerequisites`, pontos
e tempo) e `docs/learning-objectives.md`, incluindo a tabela "objetivo → como é verificado".

### 4. Implemente o código inicial

- Deve compilar e executar; os testes devem falhar **por comportamento**.
- Problemas plantados devem ser realistas (o tipo de bug que se encontra em produção).
- O participante investiga e modifica o projeto — não reescreve do zero.

### 5. Escreva os testes

- Verifique comportamento: execução, sanitizers, race detector, respostas HTTP, estado
  de containers/clusters. Não use `grep` no código do participante como critério.
- Cubra casos de borda e **soluções erradas plausíveis** (corrigir só metade do problema
  deve reprovar).
- Determinismo: sementes fixas, limites de tempo generosos (≈10× a solução), sem rede.
- Cada critério imprime ✔/✘ com uma explicação útil.

### 6. Escreva `start.sh` e `test.sh`

Use os helpers de `scripts/lib/common.sh` (ver [challenge-format.md](challenge-format.md)).
Rode `./start.sh` duas vezes: a segunda execução não pode quebrar nem duplicar recursos.

### 7. Documente

`README.md` com as 12 seções, `docs/hints.md` com níveis progressivos. Cite referências
específicas (livro + capítulo/item, documentação oficial).

### 8. Escreva a solução de referência — fora deste repositório

Coloque-a no repositório privado de soluções, espelhando os caminhos dos arquivos
editáveis (`gitquest-solutions/desafio-33/src/...`) com um `SOLUTION.md`.
**Nunca** a commite aqui, nem temporariamente: o histórico do Git é público.

### 9. Valide

```bash
./scripts/gitquest validate                 # contrato
git add -A
python3 scripts/lib/gq.py checksum .        # copie para starter_checksum no challenge.yaml
git add -A
./scripts/validate-branch.sh                # código inicial: os testes DEVEM falhar
# no repositório privado:
./verify-solutions.sh desafio-33            # solução: os testes DEVEM passar
```

### 10. Submeta

```bash
git commit -m "feat(desafio-33): Deadlock no worker pool"
git push origin desafio-33
```

Na `main`, adicione (ou mude para `available`) a entrada em
`config/challenge-catalog.yaml` e a linha em `docs/challenge-catalog.md`, e abra um PR.
A CI da `main` exige que catálogo e branches estejam consistentes.

## Mudando as ferramentas compartilhadas

Arquivos listados em `config/shared-files.txt` só são editados na `main`. Depois do merge:

```bash
./scripts/sync-tooling.sh --dry-run    # mostra o que mudaria em cada desafio-*
./scripts/sync-tooling.sh              # commita a cópia em cada branch
git push origin 'refs/heads/desafio-*'
```

## Checklist de revisão

- [ ] `gitquest validate` passa; `starter_checksum` atualizado
- [ ] o código inicial reprova e a solução de referência passa (`verify-solutions.sh`)
- [ ] pelo menos duas soluções parciais reprovam
- [ ] `start.sh` idempotente; mensagens de erro compreensíveis
- [ ] imagens com versão fixada; nada roda como root; timeouts em tudo
- [ ] README descreve sintomas, não a causa; dicas progressivas sem código pronto
- [ ] catálogo e `docs/challenge-catalog.md` atualizados na `main`
