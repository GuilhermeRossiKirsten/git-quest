# Contribuindo com o GitQuest

Obrigado por ajudar! Há três tipos de contribuição:

1. **Novo desafio** — siga [docs/contributing-challenges.md](docs/contributing-challenges.md).
   Comece abrindo uma issue com: conceito ensinado, sintoma do código inicial e como o teste
   distingue a solução certa das erradas.
2. **Melhoria em um desafio existente** — PR contra a branch `desafio-N`. Se mudar o código
   inicial, atualize `starter_checksum` e confirme que `./scripts/validate-branch.sh`
   continua exigindo a reprovação do código inicial.
3. **Infraestrutura** (CLI, validador, CI, documentação) — PR contra a `main`. Rode
   `make selftest` e `make lint`. Arquivos de `config/shared-files.txt` são propagados às
   branches por `./scripts/sync-tooling.sh` depois do merge.

## Regras

- Soluções de referência **nunca** entram neste repositório (nem em branches, nem no histórico).
- Imagens Docker com versão fixada; nenhum segredo real; testes determinísticos.
- Textos em português do Brasil; identificadores de código em inglês.
- Prefira scripts pequenos e documentados a frameworks.

## Mensagens de commit

`tipo(escopo): descrição` — ex.: `feat(desafio-12): corrotinas que geram`,
`fix(cli): hints respeita o nível máximo`, `docs: guia do WSL2`.
