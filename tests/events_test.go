package tests

import (
	"errors"
	"fmt"
	"reflect"
	"testing"

	"desafio36/src/app"
	"desafio36/src/money"
	"desafio36/src/ordering"
)

// TestEventos_ExatamenteUmaVez: cada comando aceito registra exatamente um
// evento, com os dados do que aconteceu; comandos rejeitados não registram
// nada; PullEvents entrega cada evento uma única vez.
func TestEventos_ExatamenteUmaVez(t *testing.T) {
	o, err := ordering.NewOrder("pedido-9", "cliente-3", brlc(t, 100000))
	if err != nil {
		t.Fatalf("NewOrder: %v", err)
	}
	expect := func(step string, want ...ordering.Event) {
		t.Helper()
		got := o.PullEvents()
		if len(want) == 0 && len(got) == 0 {
			return
		}
		if !reflect.DeepEqual(got, want) {
			t.Errorf("%s: eventos = %v %+v; esperado %v %+v", step, eventNames(got), got, eventNames(want), want)
		}
		for _, e := range got {
			if e != nil && e.AggregateID() != "pedido-9" {
				t.Errorf("%s: %s.AggregateID() = %q; esperado pedido-9", step, e.EventName(), e.AggregateID())
			}
		}
	}

	expect("NewOrder", ordering.OrderCreated{OrderID: "pedido-9", CustomerID: "cliente-3"})
	expect("segundo PullEvents sem comandos no meio")

	must(t, "AddLine(A)", o.AddLine("A", 2, brlc(t, 1500)))
	expect("AddLine", ordering.LineAdded{OrderID: "pedido-9", SKU: "A", Quantity: 2, UnitPrice: brlc(t, 1500)})

	if err := o.AddLine("B", 1, brlc(t, 99000)); err == nil {
		t.Errorf("AddLine acima do limite foi aceito")
	}
	expect("AddLine rejeitado (crédito)")

	must(t, "ChangeQuantity(A, 3)", o.ChangeQuantity("A", 3))
	expect("ChangeQuantity", ordering.LineQuantityChanged{OrderID: "pedido-9", SKU: "A", Quantity: 3})

	must(t, "ApplyDiscount(20)", o.ApplyDiscount(20))
	expect("ApplyDiscount", ordering.DiscountApplied{OrderID: "pedido-9", Percent: 20})
	if err := o.ApplyDiscount(20); err == nil {
		t.Errorf("segundo desconto foi aceito")
	}
	expect("ApplyDiscount repetido")

	must(t, "Confirm", o.Confirm())
	expect("Confirm", ordering.OrderConfirmed{OrderID: "pedido-9", Total: brlc(t, 3600)})
	if err := o.Confirm(); err == nil {
		t.Errorf("segunda confirmação foi aceita")
	}
	expect("Confirm repetido")

	must(t, "Ship", o.Ship())
	expect("Ship", ordering.OrderShipped{OrderID: "pedido-9"})
	if err := o.Cancel("tarde"); err == nil {
		t.Errorf("cancelar pedido enviado foi aceito")
	}
	expect("Cancel rejeitado (já enviado)")

	t.Run("cancelamento", func(t *testing.T) {
		c := newOrder(t, 1000)
		if err := c.Confirm(); err == nil {
			t.Errorf("confirmar pedido vazio foi aceito")
		}
		if evs := c.PullEvents(); len(evs) != 0 {
			t.Errorf("Confirm rejeitado registrou %v", eventNames(evs))
		}
		must(t, "Cancel", c.Cancel("cliente desistiu"))
		evs := c.PullEvents()
		want := []ordering.Event{ordering.OrderCancelled{OrderID: "pedido-1", Reason: "cliente desistiu"}}
		if !reflect.DeepEqual(evs, want) {
			t.Errorf("Cancel: eventos = %+v; esperado %+v", evs, want)
		}
	})
}

// ---- dublês para o serviço de aplicação ----

var errDiskFull = errors.New("disco cheio (falha simulada)")

type fakeRepo struct {
	orders   map[string]*ordering.Order
	saves    int
	failNext bool
}

func (r *fakeRepo) Load(id string) (*ordering.Order, error) {
	o, ok := r.orders[id]
	if !ok {
		return nil, fmt.Errorf("pedido %q não encontrado", id)
	}
	return o, nil
}

func (r *fakeRepo) Save(o *ordering.Order) error {
	if r.failNext {
		r.failNext = false
		return errDiskFull
	}
	r.orders[o.ID()] = o
	r.saves++
	return nil
}

type published struct {
	names       []string
	savesBefore int // quantos Save bem-sucedidos já tinham acontecido
}

type fakePublisher struct {
	repo  *fakeRepo
	calls []published
}

func (p *fakePublisher) Publish(evs []ordering.Event) error {
	if len(evs) == 0 {
		return nil
	}
	p.calls = append(p.calls, published{names: eventNames(evs), savesBefore: p.repo.saves})
	return nil
}

// TestServico_PublicaSoAposSalvar: o serviço de aplicação só publica eventos
// de uma transação que foi gravada, e cada evento é publicado uma única vez.
func TestServico_PublicaSoAposSalvar(t *testing.T) {
	repo := &fakeRepo{orders: map[string]*ordering.Order{}}
	pub := &fakePublisher{repo: repo}
	svc := app.NewService(repo, pub)

	// step executa um comando e confere o que foi publicado por ele.
	step := func(name string, run func() error, wantErr error, wantEvents ...string) {
		t.Helper()
		calls, saves := len(pub.calls), repo.saves
		err := run()
		switch {
		case wantErr == nil && err != nil:
			t.Errorf("%s: erro inesperado %v", name, err)
		case wantErr != nil && !errors.Is(err, wantErr):
			t.Errorf("%s: erro = %v; esperado errors.Is(err, %v)", name, err, wantErr)
		}
		var got []string
		for _, c := range pub.calls[calls:] {
			got = append(got, c.names...)
			if c.savesBefore <= saves {
				t.Errorf("%s: eventos %v publicados ANTES de o pedido ser salvo", name, c.names)
			}
		}
		if !reflect.DeepEqual(got, wantEvents) && (len(got) != 0 || len(wantEvents) != 0) {
			t.Errorf("%s: publicou %v; esperado %v", name, got, wantEvents)
		}
	}

	limit := money.MustNew(50000, brl)
	step("PlaceOrder", func() error { return svc.PlaceOrder("p-1", "c-1", limit) }, nil, "OrderCreated")
	step("AddLine", func() error { return svc.AddLine("p-1", "A", 2, money.MustNew(1000, brl)) }, nil, "LineAdded")
	step("AddLine quantidade 0 (rejeitado pelo domínio)",
		func() error { return svc.AddLine("p-1", "B", 0, money.MustNew(1000, brl)) }, ordering.ErrInvalidQuantity)
	step("AddLine acima do limite (rejeitado pelo domínio)",
		func() error { return svc.AddLine("p-1", "B", 1, money.MustNew(49000, brl)) }, ordering.ErrCreditLimitExceeded)
	step("ApplyDiscount", func() error { return svc.ApplyDiscount("p-1", 10) }, nil, "DiscountApplied")

	if o, err := repo.Load("p-1"); err == nil {
		if got := o.Total().Cents(); got != 1800 {
			t.Errorf("total salvo = %d centavos; esperado 1800 (2 × 10,00 − 10%%)", got)
		}
	}

	repo.failNext = true
	step("Confirm com falha ao salvar", func() error { return svc.Confirm("p-1") }, errDiskFull)

	// Um segundo pedido segue o fluxo completo sem falhas.
	step("PlaceOrder p-2", func() error { return svc.PlaceOrder("p-2", "c-2", limit) }, nil, "OrderCreated")
	step("AddLine p-2", func() error { return svc.AddLine("p-2", "X", 1, money.MustNew(500, brl)) }, nil, "LineAdded")
	step("Confirm p-2", func() error { return svc.Confirm("p-2") }, nil, "OrderConfirmed")
	step("Ship p-2", func() error { return svc.Ship("p-2") }, nil, "OrderShipped")
	step("Cancel p-2 enviado (rejeitado)", func() error { return svc.Cancel("p-2", "tarde") }, ordering.ErrAlreadyShipped)
	step("AddLine p-2 enviado (rejeitado)",
		func() error { return svc.AddLine("p-2", "Y", 1, money.MustNew(500, brl)) }, ordering.ErrNotDraft)

	repo.failNext = true
	step("PlaceOrder com falha ao salvar", func() error { return svc.PlaceOrder("p-3", "c-3", limit) }, errDiskFull)
}
