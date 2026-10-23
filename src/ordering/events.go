package ordering

import "desafio36/src/money"

// Event é um evento de domínio: o registro de algo que JÁ aconteceu com um
// pedido. Os nomes estão no passado de propósito.
type Event interface {
	EventName() string
	AggregateID() string
}

// OrderCreated: um pedido foi aberto (rascunho).
type OrderCreated struct {
	OrderID    string
	CustomerID string
}

// LineAdded: um item foi incluído no pedido.
type LineAdded struct {
	OrderID   string
	SKU       string
	Quantity  int
	UnitPrice money.Money
}

// LineQuantityChanged: a quantidade de um item mudou.
type LineQuantityChanged struct {
	OrderID  string
	SKU      string
	Quantity int
}

// DiscountApplied: um cupom de desconto percentual foi aplicado.
type DiscountApplied struct {
	OrderID string
	Percent int
}

// OrderConfirmed: o cliente fechou o pedido; Total é o valor cobrado.
type OrderConfirmed struct {
	OrderID string
	Total   money.Money
}

// OrderShipped: o pedido saiu para entrega.
type OrderShipped struct {
	OrderID string
}

// OrderCancelled: o pedido foi cancelado.
type OrderCancelled struct {
	OrderID string
	Reason  string
}

func (e OrderCreated) EventName() string        { return "OrderCreated" }
func (e LineAdded) EventName() string           { return "LineAdded" }
func (e LineQuantityChanged) EventName() string { return "LineQuantityChanged" }
func (e DiscountApplied) EventName() string     { return "DiscountApplied" }
func (e OrderConfirmed) EventName() string      { return "OrderConfirmed" }
func (e OrderShipped) EventName() string        { return "OrderShipped" }
func (e OrderCancelled) EventName() string      { return "OrderCancelled" }

func (e OrderCreated) AggregateID() string        { return e.OrderID }
func (e LineAdded) AggregateID() string           { return e.OrderID }
func (e LineQuantityChanged) AggregateID() string { return e.OrderID }
func (e DiscountApplied) AggregateID() string     { return e.OrderID }
func (e OrderConfirmed) AggregateID() string      { return e.OrderID }
func (e OrderShipped) AggregateID() string        { return e.OrderID }
func (e OrderCancelled) AggregateID() string      { return e.OrderID }
