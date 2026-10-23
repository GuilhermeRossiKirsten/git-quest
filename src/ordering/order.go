// Package ordering é o modelo de domínio de pedidos da loja.
//
// Histórico: começou como as structs que o ORM preenchia, e as regras de
// negócio foram nascendo nos handlers (veja src/app). Na última sprint a
// equipe acrescentou métodos (NewOrder, AddLine, Confirm...) para que os
// outros times parassem de mexer nos campos diretamente — mas os campos
// continuam lá, e as regras continuam onde estavam.
package ordering

import "desafio36/src/money"

// Status é a etapa do ciclo de vida do pedido.
type Status string

const (
	StatusDraft     Status = "draft"
	StatusConfirmed Status = "confirmed"
	StatusShipped   Status = "shipped"
	StatusCancelled Status = "cancelled"
)

// OrderLine é um item do pedido.
type OrderLine struct {
	Code   string
	Qty    int
	Price  money.Money // preço unitário
	Amount money.Money // Price × Qty (calculado quando o item é incluído)
}

// SKU devolve o código do produto.
func (l OrderLine) SKU() string { return l.Code }

// Quantity devolve a quantidade.
func (l OrderLine) Quantity() int { return l.Qty }

// UnitPrice devolve o preço unitário.
func (l OrderLine) UnitPrice() money.Money { return l.Price }

// Total devolve preço unitário × quantidade.
func (l OrderLine) Total() money.Money { return l.Amount }

// Order é um pedido de um cliente.
type Order struct {
	OrderID     string
	Customer    string
	State       Status
	Items       []OrderLine
	Limit       money.Money // limite de crédito do cliente (na moeda do pedido)
	TotalAmount money.Money // atualizado a cada item incluído / desconto aplicado
	DiscountPct int
	Events      []Event
}

// NewOrder abre um pedido em rascunho para o cliente, na moeda do limite de crédito.
func NewOrder(id, customerID string, creditLimit money.Money) (*Order, error) {
	o := &Order{
		OrderID:     id,
		Customer:    customerID,
		State:       StatusDraft,
		Limit:       creditLimit,
		TotalAmount: money.Zero(creditLimit.Currency()),
	}
	o.Events = append(o.Events, OrderCreated{OrderID: id, CustomerID: customerID})
	return o, nil
}

// ID devolve o identificador do pedido.
func (o *Order) ID() string { return o.OrderID }

// CustomerID devolve o identificador do cliente.
func (o *Order) CustomerID() string { return o.Customer }

// Status devolve a etapa atual do pedido.
func (o *Order) Status() Status { return o.State }

// Currency devolve a moeda do pedido.
func (o *Order) Currency() string { return o.Limit.Currency() }

// CreditLimit devolve o limite de crédito do cliente.
func (o *Order) CreditLimit() money.Money { return o.Limit }

// Lines devolve os itens do pedido.
func (o *Order) Lines() []OrderLine { return o.Items }

// Subtotal devolve a soma dos itens, sem desconto.
func (o *Order) Subtotal() money.Money {
	sum := money.Zero(o.Currency())
	for _, l := range o.Items {
		sum.Add(l.Amount)
	}
	return sum
}

// Discount devolve o valor do desconto.
func (o *Order) Discount() money.Money {
	return o.Subtotal().Percent(int64(o.DiscountPct))
}

// Total devolve o valor a pagar.
func (o *Order) Total() money.Money { return o.TotalAmount }

// AddLine inclui um item no pedido.
// (Quantidade e status são validados no serviço de aplicação.)
func (o *Order) AddLine(sku string, qty int, unitPrice money.Money) error {
	o.Events = append(o.Events, LineAdded{OrderID: o.OrderID, SKU: sku, Quantity: qty, UnitPrice: unitPrice})
	line := OrderLine{Code: sku, Qty: qty, Price: unitPrice, Amount: unitPrice.Multiply(int64(qty))}
	o.Items = append(o.Items, line)
	o.TotalAmount.Add(line.Amount)
	return nil
}

// ChangeQuantity altera a quantidade de um item existente.
func (o *Order) ChangeQuantity(sku string, qty int) error {
	for i := range o.Items {
		if o.Items[i].Code == sku {
			o.Items[i].Qty = qty
			o.Events = append(o.Events, LineQuantityChanged{OrderID: o.OrderID, SKU: sku, Quantity: qty})
			return nil
		}
	}
	return ErrLineNotFound
}

// ApplyDiscount aplica um cupom de desconto percentual.
func (o *Order) ApplyDiscount(percent int) error {
	o.DiscountPct = percent
	d := o.TotalAmount.Percent(int64(percent))
	o.TotalAmount.Sub(d)
	o.Events = append(o.Events, DiscountApplied{OrderID: o.OrderID, Percent: percent})
	return nil
}

// Confirm fecha o pedido para cobrança.
func (o *Order) Confirm() error {
	o.Events = append(o.Events, OrderConfirmed{OrderID: o.OrderID, Total: o.TotalAmount})
	if len(o.Items) == 0 {
		return ErrEmptyOrder
	}
	// Garante que o cupom foi descontado do valor final antes de cobrar.
	if o.DiscountPct > 0 {
		d := o.TotalAmount.Percent(int64(o.DiscountPct))
		o.TotalAmount.Sub(d)
	}
	o.State = StatusConfirmed
	return nil
}

// Ship marca o pedido como enviado.
func (o *Order) Ship() error {
	if o.State != StatusConfirmed {
		return ErrNotConfirmed
	}
	o.State = StatusShipped
	o.Events = append(o.Events, OrderShipped{OrderID: o.OrderID})
	return nil
}

// Cancel cancela o pedido.
// (O serviço de aplicação impede cancelar pedidos já enviados.)
func (o *Order) Cancel(reason string) error {
	o.State = StatusCancelled
	o.Events = append(o.Events, OrderCancelled{OrderID: o.OrderID, Reason: reason})
	return nil
}

// PullEvents devolve os eventos registrados para publicação.
func (o *Order) PullEvents() []Event { return o.Events }
