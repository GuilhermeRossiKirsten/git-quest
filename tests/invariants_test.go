package tests

import (
	"errors"
	"testing"

	"desafio36/src/money"
	"desafio36/src/ordering"
)

// invariantCase: prepara um pedido, executa UM comando que viola uma regra e
// exige: erro reconhecível por errors.Is, estado observável intacto e nenhum
// evento registrado.
type invariantCase struct {
	name  string
	setup func(t *testing.T, o *ordering.Order)
	cmd   func(t *testing.T, o *ordering.Order) error
	want  error
}

func runInvariantCases(t *testing.T, limit int64, cases []invariantCase) {
	t.Helper()
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			o := newOrder(t, limit)
			if c.setup != nil {
				c.setup(t, o)
			}
			o.PullEvents()
			before := view(o)
			err := c.cmd(t, o)
			if !isErr(err, c.want) {
				t.Errorf("%s: erro = %v; esperado errors.Is(err, %v)", c.name, err, c.want)
			}
			if after := view(o); !sameView(before, after) {
				t.Errorf("%s: comando rejeitado alterou o pedido:\n  antes:  %v\n  depois: %v", c.name, before, after)
			}
			if evs := o.PullEvents(); len(evs) != 0 {
				t.Errorf("%s: comando rejeitado registrou eventos %v", c.name, eventNames(evs))
			}
		})
	}
}

func addA(t *testing.T, o *ordering.Order) {
	must(t, "AddLine(A, 2, 10.00)", o.AddLine("A", 2, brlc(t, 1000)))
}

func confirmed(t *testing.T, o *ordering.Order) {
	addA(t, o)
	must(t, "Confirm", o.Confirm())
}

// TestInvariantes_Itens: regras de inclusão e alteração de itens, inclusive o
// limite de crédito, valem para QUALQUER chamador do agregado.
func TestInvariantes_Itens(t *testing.T) {
	runInvariantCases(t, 1000000, []invariantCase{
		{name: "quantidade zero", want: ordering.ErrInvalidQuantity,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("A", 0, brlc(t, 100)) }},
		{name: "quantidade negativa", want: ordering.ErrInvalidQuantity,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("A", -3, brlc(t, 100)) }},
		{name: "quantidade acima de 10000", want: ordering.ErrInvalidQuantity,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("A", 10001, brlc(t, 1)) }},
		{name: "SKU vazio", want: ordering.ErrInvalidArgument,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("", 1, brlc(t, 100)) }},
		{name: "preço zero", want: ordering.ErrInvalidArgument,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("A", 1, brlc(t, 0)) }},
		{name: "preço negativo", want: ordering.ErrInvalidArgument,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("A", 1, brlc(t, -100)) }},
		{name: "preço em outra moeda", want: money.ErrCurrencyMismatch,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("A", 1, usdc(t, 100)) }},
		{name: "SKU repetido", want: ordering.ErrDuplicateSKU, setup: addA,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("A", 1, brlc(t, 1000)) }},
		{name: "item estoura o limite de crédito", want: ordering.ErrCreditLimitExceeded, setup: addA,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("B", 1, brlc(t, 998001)) }},
		{name: "incluir item em pedido confirmado", want: ordering.ErrNotDraft, setup: confirmed,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.AddLine("B", 1, brlc(t, 100)) }},
		{name: "incluir item em pedido cancelado", want: ordering.ErrNotDraft,
			setup: func(t *testing.T, o *ordering.Order) { must(t, "Cancel", o.Cancel("desistiu")) },
			cmd:   func(t *testing.T, o *ordering.Order) error { return o.AddLine("B", 1, brlc(t, 100)) }},
		{name: "alterar item inexistente", want: ordering.ErrLineNotFound, setup: addA,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.ChangeQuantity("Z", 1) }},
		{name: "alterar quantidade para zero", want: ordering.ErrInvalidQuantity, setup: addA,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.ChangeQuantity("A", 0) }},
		{name: "alterar quantidade estoura o limite", want: ordering.ErrCreditLimitExceeded, setup: addA,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.ChangeQuantity("A", 1001) }},
		{name: "alterar item de pedido confirmado", want: ordering.ErrNotDraft, setup: confirmed,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.ChangeQuantity("A", 1) }},
	})

	t.Run("limite de crédito considera o desconto", func(t *testing.T) {
		o := newOrder(t, 10000)
		must(t, "ApplyDiscount(10)", o.ApplyDiscount(10))
		// 110,00 com 10% de desconto = 99,00 <= 100,00: cabe no limite.
		if err := o.AddLine("A", 1, brlc(t, 11000)); err != nil {
			t.Errorf("AddLine de 110,00 com 10%% de desconto e limite 100,00 foi rejeitado: %v", err)
		}
		// exatamente no limite também cabe
		o2 := newOrder(t, 10000)
		if err := o2.AddLine("A", 4, brlc(t, 2500)); err != nil {
			t.Errorf("AddLine que atinge exatamente o limite foi rejeitado: %v", err)
		}
	})

	t.Run("NewOrder valida os argumentos", func(t *testing.T) {
		bad := []struct {
			name, id, cust string
			limit          money.Money
		}{
			{"id vazio", "", "c", brlc(t, 100)},
			{"cliente vazio", "p", "", brlc(t, 100)},
			{"limite zero", "p", "c", brlc(t, 0)},
			{"limite negativo", "p", "c", brlc(t, -1)},
			{"limite sem moeda", "p", "c", money.Money{}},
		}
		for _, b := range bad {
			o, err := ordering.NewOrder(b.id, b.cust, b.limit)
			if !errors.Is(err, ordering.ErrInvalidArgument) || o != nil {
				t.Errorf("%s: NewOrder = (%v, %v); esperado (nil, ErrInvalidArgument)", b.name, o != nil, err)
			}
		}
	})
}

// TestInvariantes_CicloDeVida: transições de status e o cupom de desconto.
func TestInvariantes_CicloDeVida(t *testing.T) {
	shipped := func(t *testing.T, o *ordering.Order) {
		confirmed(t, o)
		must(t, "Ship", o.Ship())
	}
	cancelled := func(t *testing.T, o *ordering.Order) { must(t, "Cancel", o.Cancel("x")) }
	runInvariantCases(t, 100000, []invariantCase{
		{name: "confirmar pedido vazio", want: ordering.ErrEmptyOrder,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.Confirm() }},
		{name: "confirmar duas vezes", want: ordering.ErrNotDraft, setup: confirmed,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.Confirm() }},
		{name: "confirmar pedido cancelado", want: ordering.ErrNotDraft,
			setup: func(t *testing.T, o *ordering.Order) { addA(t, o); cancelled(t, o) },
			cmd:   func(t *testing.T, o *ordering.Order) error { return o.Confirm() }},
		{name: "enviar rascunho", want: ordering.ErrNotConfirmed, setup: addA,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.Ship() }},
		{name: "enviar pedido cancelado", want: ordering.ErrNotConfirmed,
			setup: func(t *testing.T, o *ordering.Order) { confirmed(t, o); cancelled(t, o) },
			cmd:   func(t *testing.T, o *ordering.Order) error { return o.Ship() }},
		{name: "enviar duas vezes", want: ordering.ErrNotConfirmed, setup: shipped,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.Ship() }},
		{name: "cancelar pedido enviado", want: ordering.ErrAlreadyShipped, setup: shipped,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.Cancel("tarde demais") }},
		{name: "cancelar duas vezes", want: ordering.ErrAlreadyCancelled, setup: cancelled,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.Cancel("de novo") }},
		{name: "desconto duas vezes", want: ordering.ErrDiscountAlreadyApplied,
			setup: func(t *testing.T, o *ordering.Order) { addA(t, o); must(t, "ApplyDiscount(10)", o.ApplyDiscount(10)) },
			cmd:   func(t *testing.T, o *ordering.Order) error { return o.ApplyDiscount(5) }},
		{name: "desconto de 0%", want: ordering.ErrInvalidDiscount, setup: addA,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.ApplyDiscount(0) }},
		{name: "desconto acima de 100%", want: ordering.ErrInvalidDiscount, setup: addA,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.ApplyDiscount(101) }},
		{name: "desconto em pedido confirmado", want: ordering.ErrNotDraft, setup: confirmed,
			cmd: func(t *testing.T, o *ordering.Order) error { return o.ApplyDiscount(10) }},
	})

	t.Run("transições permitidas", func(t *testing.T) {
		o := newOrder(t, 100000)
		if o.Status() != ordering.StatusDraft {
			t.Fatalf("pedido novo com status %q; esperado draft", o.Status())
		}
		if err := o.Cancel("mudou de ideia"); err != nil || o.Status() != ordering.StatusCancelled {
			t.Errorf("cancelar rascunho: err=%v status=%s", err, o.Status())
		}
		o = newOrder(t, 100000)
		confirmed(t, o)
		if err := o.Cancel("sem estoque"); err != nil || o.Status() != ordering.StatusCancelled {
			t.Errorf("cancelar pedido confirmado: err=%v status=%s", err, o.Status())
		}
		o = newOrder(t, 100000)
		confirmed(t, o)
		if err := o.Ship(); err != nil || o.Status() != ordering.StatusShipped {
			t.Errorf("enviar pedido confirmado: err=%v status=%s", err, o.Status())
		}
	})
}

// TestTotal_SempreConsistente: subtotal, desconto e total refletem SEMPRE os
// itens atuais; o desconto incide uma única vez.
func TestTotal_SempreConsistente(t *testing.T) {
	o := newOrder(t, 1000000)
	check := func(step string, sub, disc, total int64) {
		t.Helper()
		v := view(o)
		if v.Subtotal != sub || v.Discount != disc || v.Total != total || v.TotalCur != brl || v.SubtotalCur != brl {
			t.Errorf("%s: subtotal=%d desconto=%d total=%d %s; esperado %d/%d/%d BRL", step, v.Subtotal, v.Discount, v.Total, v.TotalCur, sub, disc, total)
		}
		for _, l := range v.Lines {
			if l.Total != l.Unit*int64(l.Qty) {
				t.Errorf("%s: item %s com total %d ≠ %d × %d", step, l.SKU, l.Total, l.Unit, l.Qty)
			}
		}
	}
	check("pedido novo", 0, 0, 0)
	must(t, "AddLine(A, 2, 10.50)", o.AddLine("A", 2, brlc(t, 1050)))
	must(t, "AddLine(B, 1, 0.29)", o.AddLine("B", 1, brlc(t, 29)))
	check("depois de incluir A e B", 2129, 0, 2129)
	must(t, "ChangeQuantity(A, 3)", o.ChangeQuantity("A", 3))
	check("depois de mudar A para 3", 3179, 0, 3179)
	must(t, "ApplyDiscount(10)", o.ApplyDiscount(10))
	check("depois do cupom de 10%", 3179, 318, 2861) // 317,9 → 318
	must(t, "ChangeQuantity(B, 5)", o.ChangeQuantity("B", 5))
	check("depois de mudar B para 5 (desconto recalculado)", 3295, 330, 2965) // 329,5 → 330
	o.PullEvents()
	must(t, "Confirm", o.Confirm())
	check("depois de confirmar (desconto não incide de novo)", 3295, 330, 2965)
	for _, e := range o.PullEvents() {
		if c, ok := e.(ordering.OrderConfirmed); ok && c.Total.Cents() != 2965 {
			t.Errorf("OrderConfirmed.Total = %v; esperado BRL 29.65 (o total do pedido)", c.Total)
		}
	}
}
