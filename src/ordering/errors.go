package ordering

import "errors"

// Erros de domínio. Os chamadores (e os testes) os reconhecem com errors.Is;
// o texto pode trazer detalhes, desde que o erro original esteja embrulhado.
var (
	ErrInvalidArgument        = errors.New("pedido: argumento inválido")
	ErrInvalidQuantity        = errors.New("pedido: quantidade inválida")
	ErrDuplicateSKU           = errors.New("pedido: item já existe no pedido")
	ErrLineNotFound           = errors.New("pedido: item não encontrado")
	ErrCreditLimitExceeded    = errors.New("pedido: limite de crédito excedido")
	ErrInvalidDiscount        = errors.New("pedido: desconto inválido")
	ErrDiscountAlreadyApplied = errors.New("pedido: desconto já aplicado")
	ErrEmptyOrder             = errors.New("pedido: pedido sem itens")
	ErrNotDraft               = errors.New("pedido: pedido não está em rascunho")
	ErrNotConfirmed           = errors.New("pedido: pedido não está confirmado")
	ErrAlreadyShipped         = errors.New("pedido: pedido já foi enviado")
	ErrAlreadyCancelled       = errors.New("pedido: pedido já foi cancelado")
)
