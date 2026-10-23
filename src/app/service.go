// Package app contém o serviço de aplicação de pedidos: é ele que os handlers
// HTTP e os consumidores de fila chamam. Cada método carrega o pedido, executa
// um comando, salva e publica os eventos de domínio para os outros contextos
// (faturamento, estoque, e-mail).
package app

import (
	"errors"
	"fmt"

	"desafio36/src/money"
	"desafio36/src/ordering"
)

// Repository guarda pedidos. Save é a "transação": se falhar, nada aconteceu.
type Repository interface {
	Load(id string) (*ordering.Order, error)
	Save(o *ordering.Order) error
}

// Publisher entrega eventos de domínio aos outros contextos.
type Publisher interface {
	Publish(events []ordering.Event) error
}

// Service orquestra os casos de uso de pedidos.
type Service struct {
	repo Repository
	pub  Publisher
}

// NewService cria o serviço.
func NewService(repo Repository, pub Publisher) *Service {
	return &Service{repo: repo, pub: pub}
}

// PlaceOrder abre um pedido em rascunho.
func (s *Service) PlaceOrder(id, customerID string, creditLimit money.Money) error {
	o, err := ordering.NewOrder(id, customerID, creditLimit)
	if err != nil {
		return err
	}
	return s.publishAndSave(o)
}

// AddLine inclui um item no pedido.
func (s *Service) AddLine(orderID, sku string, qty int, unitPrice money.Money) error {
	if qty <= 0 {
		return fmt.Errorf("quantidade inválida: %d", qty)
	}
	o, err := s.repo.Load(orderID)
	if err != nil {
		return err
	}
	if o.State != ordering.StatusDraft {
		return errors.New("pedido não pode mais ser alterado")
	}
	if unitPrice.Code != o.Limit.Code {
		return money.ErrCurrencyMismatch
	}
	if err := o.AddLine(sku, qty, unitPrice); err != nil {
		return err
	}
	return s.publishAndSave(o)
}

// ApplyDiscount aplica um cupom percentual.
func (s *Service) ApplyDiscount(orderID string, percent int) error {
	o, err := s.repo.Load(orderID)
	if err != nil {
		return err
	}
	if o.DiscountPct > 0 {
		return ordering.ErrDiscountAlreadyApplied
	}
	if err := o.ApplyDiscount(percent); err != nil {
		return err
	}
	return s.publishAndSave(o)
}

// Confirm fecha o pedido, conferindo o limite de crédito do cliente.
func (s *Service) Confirm(orderID string) error {
	o, err := s.repo.Load(orderID)
	if err != nil {
		return err
	}
	if o.TotalAmount.Amount > o.Limit.Amount {
		return ordering.ErrCreditLimitExceeded
	}
	if err := o.Confirm(); err != nil {
		return err
	}
	return s.publishAndSave(o)
}

// Ship marca o pedido como enviado.
func (s *Service) Ship(orderID string) error {
	o, err := s.repo.Load(orderID)
	if err != nil {
		return err
	}
	if err := o.Ship(); err != nil {
		return err
	}
	return s.publishAndSave(o)
}

// Cancel cancela o pedido, se ele ainda não foi enviado.
func (s *Service) Cancel(orderID, reason string) error {
	o, err := s.repo.Load(orderID)
	if err != nil {
		return err
	}
	if o.State == ordering.StatusShipped {
		return ordering.ErrAlreadyShipped
	}
	if err := o.Cancel(reason); err != nil {
		return err
	}
	return s.publishAndSave(o)
}

// publishAndSave avisa os outros contextos o quanto antes e depois grava.
func (s *Service) publishAndSave(o *ordering.Order) error {
	if err := s.pub.Publish(o.PullEvents()); err != nil {
		return err
	}
	return s.repo.Save(o)
}
