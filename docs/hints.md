# Dicas — desafio-36

Use `./scripts/gitquest hints <nível>`; cada nível revela um pouco mais.

## Nível 1

Escolha uma regra do contrato, por exemplo "não se inclui item num pedido
confirmado". Procure **todos** os lugares em que ela é verificada. Agora
procure todos os caminhos que levam a `AddLine` (o serviço, o app novo, um
teste, o time de faturamento importando o pacote). Quantos desses caminhos
passam pela verificação?

Faça a mesma pergunta para o dinheiro: o que é guardado dentro de um `Money`
e o que acontece com `0.29` quando ele vira centavos? E quem muda quando você
escreve `a.Add(b)`?

## Nível 2

Lugares para olhar:

- `src/money/money.go`: o tipo do campo de valor, a conversão em `Cents()`, os
  receptores `*Money` de `Add`/`Sub`, a falta de checagem de moeda em `Add`, e
  como `Parse` usa `strconv.ParseFloat`.
- `src/ordering/order.go`: `TotalAmount` é um cache — quem o atualiza e quem
  esquece? `Confirm` desconta o cupom de novo. Em que linha cada comando
  registra o evento: antes ou depois de decidir se aceita? `Lines()` e
  `PullEvents()` devolvem o próprio slice interno.
- `src/app/service.go`: as regras que estão aqui (quantidade, status, moeda,
  crédito, cancelar enviado, desconto repetido) são exatamente as que faltam
  no agregado. E `publishAndSave` faz as coisas na ordem do nome.
- Cuidado ao consertar `Money`: o código de `ordering` (e o próprio `Subtotal`)
  hoje *depende* de `Add` alterar o receptor.

## Nível 3

Abordagem sugerida, em passos pequenos, rodando `./test.sh` entre eles:

1. `Money` com `cents int64` e `currency string` não exportados; todos os
   métodos com receptor por valor devolvendo um valor novo; `Add`/`Sub`
   conferem a moeda; `Parse` lê os dígitos à mão (parte inteira, ponto,
   até duas casas) sem passar por `float64`; `Percent` faz a conta em
   inteiros e arredonda olhando o resto da divisão por 100.
2. Torne não exportados os campos de `Order` e `OrderLine`. O compilador vai
   apontar cada lugar de fora que mexia neles — é a lista de regras que precisa
   mudar de casa.
3. Em cada comando do agregado: primeiro **todas** as validações (status,
   argumentos, moeda, duplicidade, crédito calculado sobre os itens
   *propostos*), depois a mudança de estado, por último o registro do evento.
4. Não guarde total nem subtotal: calcule-os a partir dos itens e do
   percentual do cupom sempre que pedirem.
5. `Lines()` devolve uma cópia; `PullEvents()` devolve os eventos e esvazia a
   lista interna.
6. No serviço, apague as regras de negócio e deixe só a orquestração:
   carregar → comando → salvar → (se salvou) publicar `PullEvents()`.

## Nível 4

Se o teste de propriedade falhar, a mensagem traz a semente, o número da
sequência, o passo e os últimos comandos. Repita esses comandos num teste seu
(num arquivo novo em `src/ordering`, por exemplo) e compare, depois de cada
um, `Subtotal`, `Discount`, `Total` e `PullEvents()` com a conta feita à mão.
Divergências comuns: o crédito conferido com o desconto antigo, o arredondamento
feito em cada item em vez de no subtotal, ou um evento registrado antes de uma
validação que ainda pode rejeitar o comando.
