# Dicas — desafio-24

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Desenhe a linha do tempo de `GetProduct` e de `SaveProduct`, comando a
comando (cada `GET`/`SET`/`DEL` no Redis e cada consulta ao banco). Depois
coloque duas linhas do tempo lado a lado e pergunte:

- "Se uma leitura acontecer **entre** dois passos do `SaveProduct`, o que ela
  encontra no cache e no banco? O que ela deixa no cache?"
- "E se uma leitura começou **antes** do `SaveProduct`, buscou o preço no banco
  e só termina **depois** dele — o que ela grava no cache?"
- "Se 50 leituras do mesmo produto chegarem juntas com o cache vazio, quantas
  vão ao banco?"
- "Quando o Redis não responde, quanto tempo cada comando espera? Quem decide?"
- "Que chave a loja `acme` e a loja `globex` usam para o produto `42`?"

`MONITOR` no `redis-cli` mostra os comandos chegando em ordem.

## Nível 2

Onde olhar:

- `SaveProduct`: em que ordem ele mexe no banco e no cache? Troque a ordem e
  refaça a linha do tempo da primeira pergunta.
- Mesmo na ordem certa, sobra uma corrida (o teste
  `TestInvalidacao_CorridaLeituraLenta` a força com `HoldNextGet`). Ela tem
  nome: *stale set*. Leia a seção 3.2.1 de "Scaling Memcache at Facebook" — a
  ideia é que quem vai preencher o cache precise de uma "permissão" que a
  gravação revoga. O Redis executa um script `EVAL` de forma atômica.
- O stampede se resolve **dentro** de cada réplica: as leituras que chegam
  enquanto uma já está buscando a mesma chave deveriam esperar por ela. Um
  `map` de chamadas em andamento protegido por mutex resolve; um mutex único
  para tudo não (outros produtos ficariam esperando).
- `cacheTTL` é uma constante. O que acontece com 300 chaves gravadas no mesmo
  segundo e com o mesmo TTL?
- O que `GetProduct` faz quando o banco diz `ErrNotFound`? Como guardar
  "não existe" no cache sem confundir com um produto?
- `internal/redisx` aplica o prazo do `ctx`. Qual prazo o serviço passa para
  o Redis? O que ele faz quando o Redis devolve erro?
- `keys.go`: duas lojas, mesmo ID. E `("loja:sp", "7")` contra `("loja", "sp:7")`?

## Nível 3

Abordagem (sem código pronto):

1. **Ordem**: grave no banco, depois apague a entrada do cache (*delete*, não
   *set*: quem lê depois busca o valor fresco). Falha ao apagar não desfaz a
   gravação — registre e siga.
2. **Stale set** — escolha uma das opções e documente a escolha:
   - *Lease*: no miss, grave `lease:<chave> = <token aleatório>` com TTL curto
     (alguns segundos) **antes** de ir ao banco; `SaveProduct` apaga a entrada
     **e** o lease; o preenchimento é um script Lua que só grava se o lease
     ainda tiver o seu token (e então o apaga).
   - *Versão*: guarde a versão do produto (o banco a incrementa) junto com o
     valor, faça `SaveProduct` gravar a versão nova no cache e só aceite
     preenchimentos condicionais ("grave se não existe ou se a versão guardada
     é menor"), tudo num script Lua. Cuidado: depois de um `DEL` não há o que
     comparar.
   - *Delete duplo atrasado* (apagar de novo depois de X ms) **não** passa: a
     leitura lenta pode terminar depois do segundo delete, e nesse meio-tempo
     o cache mente. TTL curto só limita a mentira.
3. **Stampede**: `map[chave]*chamada` + `sync.Mutex`; a primeira cria a
   chamada e busca, as outras esperam um canal que ela fecha (`select` com
   `ctx.Done()` para não esperar além do prazo). Remova a chamada do mapa ao
   terminar.
4. **TTL**: `base ± jitter` com `math/rand/v2` (ex.: 10 min ± 2 min). Negativo:
   uma marca própria no valor (ex.: um JSON com `"found": false`) e TTL de
   ~30 s. `SaveProduct` apaga a mesma chave, então um produto criado aparece na hora.
5. **Redis fora**: `context.WithTimeout` curto (100–200 ms) em **cada**
   comando; qualquer erro do cache vira "miss" e a leitura segue para o banco
   sem tentar gravar no cache.
6. **Chaves**: prefixo com namespace e versão do formato
   (`catalog:v2:product:`), loja **e** ID, cada um escapado
   (`url.QueryEscape`) ou prefixado pelo tamanho.

## Nível 4

Armadilhas comuns quando quase tudo passa:

- Com `SET ... NX` no lease, uma leitura que não conseguiu o lease (outra
  réplica está buscando) não preenche o cache — tudo bem, desde que ela ainda
  devolva o valor do banco: "sem lease" não é erro. Sem `NX`, cada leitura tem
  o seu token e só a última vale; também funciona.
- O teste de stampede passa, mas o de "outro produto" falha: o mutex está
  cobrindo a busca no banco inteira, não só o mapa de chamadas.
- Uma leitura com o Redis fora passa, a terceira não: depois de um prazo
  estourado, a próxima chamada abre outra conexão e espera de novo — cada
  comando precisa do seu prazo (e, se houver vários comandos por leitura,
  pule o preenchimento quando o `GET` já falhou).
- A chave negativa ficou com TTL de produto, ou o lease ficou sem TTL (o teste
  de TTL procura chaves sem expiração).
- Os testes rodam com `-race`: o mapa de chamadas em andamento precisa estar
  sempre sob o mutex.
