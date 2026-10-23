package tests

import (
	"fmt"
	"math/rand/v2"
	"strings"
	"testing"

	"desafio36/src/money"
	"desafio36/src/ordering"
)

// Modelo de referência (oráculo) escrito só com inteiros: diz se um comando
// deve ser aceito e qual deve ser o estado observável depois dele.
type modelLine struct {
	sku  string
	qty  int
	unit int64
}

type model struct {
	status   ordering.Status
	lines    []modelLine
	discount int // 0 = sem cupom
	limit    int64
}

func (m *model) subtotal(lines []modelLine) int64 {
	var s int64
	for _, l := range lines {
		s += l.unit * int64(l.qty)
	}
	return s
}

func discountOf(sub int64, pct int) int64 { return (sub*int64(pct) + 50) / 100 }

func (m *model) totalWith(lines []modelLine) int64 {
	sub := m.subtotal(lines)
	return sub - discountOf(sub, m.discount)
}

func (m *model) find(sku string) int {
	for i, l := range m.lines {
		if l.sku == sku {
			return i
		}
	}
	return -1
}

func (m *model) view() orderView {
	v := orderView{Status: m.status, TotalCur: brl, SubtotalCur: brl}
	for _, l := range m.lines {
		v.Lines = append(v.Lines, lineView{SKU: l.sku, Qty: l.qty, Unit: l.unit, UnitCur: brl, Total: l.unit * int64(l.qty), TotalCur: brl})
	}
	v.Subtotal = m.subtotal(m.lines)
	v.Discount = discountOf(v.Subtotal, m.discount)
	v.Total = v.Subtotal - v.Discount
	return v
}

// command: aplica no pedido real e, se o oráculo aceitar, no modelo.
type command struct {
	desc  string
	event string                      // evento esperado se aceito
	run   func(*ordering.Order) error // pedido real
	apply func(*model) bool           // oráculo: aceita? (e muda o modelo)
}

func randomCommand(r *rand.Rand) command {
	skus := []string{"A", "B", "C", "D", ""}
	sku := skus[r.IntN(len(skus))]
	qty := r.IntN(14) - 1 // -1..12
	if r.IntN(25) == 0 {
		qty = 10000 + r.IntN(2) // 10000 (válido) ou 10001 (inválido)
	}
	cents := int64(r.IntN(6000)) - 100 // inclui zero e negativos
	cur := brl
	if r.IntN(12) == 0 {
		cur = "USD"
	}
	switch k := r.IntN(100); {
	case k < 45:
		price := money.MustNew(cents, cur)
		return command{
			desc: fmt.Sprintf("AddLine(%q, %d, %v)", sku, qty, price), event: "LineAdded",
			run: func(o *ordering.Order) error { return o.AddLine(sku, qty, price) },
			apply: func(m *model) bool {
				if m.status != ordering.StatusDraft || sku == "" || qty < 1 || qty > 10000 || cur != brl || cents <= 0 || m.find(sku) >= 0 {
					return false
				}
				next := append(append([]modelLine(nil), m.lines...), modelLine{sku, qty, cents})
				if m.totalWith(next) > m.limit {
					return false
				}
				m.lines = next
				return true
			},
		}
	case k < 72:
		return command{
			desc: fmt.Sprintf("ChangeQuantity(%q, %d)", sku, qty), event: "LineQuantityChanged",
			run: func(o *ordering.Order) error { return o.ChangeQuantity(sku, qty) },
			apply: func(m *model) bool {
				i := m.find(sku)
				if m.status != ordering.StatusDraft || i < 0 || qty < 1 || qty > 10000 {
					return false
				}
				next := append([]modelLine(nil), m.lines...)
				next[i].qty = qty
				if m.totalWith(next) > m.limit {
					return false
				}
				m.lines = next
				return true
			},
		}
	case k < 82:
		pct := r.IntN(112) - 5 // -5..106
		return command{
			desc: fmt.Sprintf("ApplyDiscount(%d)", pct), event: "DiscountApplied",
			run: func(o *ordering.Order) error { return o.ApplyDiscount(pct) },
			apply: func(m *model) bool {
				if m.status != ordering.StatusDraft || m.discount != 0 || pct < 1 || pct > 100 {
					return false
				}
				m.discount = pct
				return true
			},
		}
	case k < 90:
		return command{
			desc: "Confirm()", event: "OrderConfirmed",
			run: func(o *ordering.Order) error { return o.Confirm() },
			apply: func(m *model) bool {
				if m.status != ordering.StatusDraft || len(m.lines) == 0 {
					return false
				}
				m.status = ordering.StatusConfirmed
				return true
			},
		}
	case k < 96:
		return command{
			desc: "Ship()", event: "OrderShipped",
			run: func(o *ordering.Order) error { return o.Ship() },
			apply: func(m *model) bool {
				if m.status != ordering.StatusConfirmed {
					return false
				}
				m.status = ordering.StatusShipped
				return true
			},
		}
	default:
		return command{
			desc: "Cancel(\"motivo\")", event: "OrderCancelled",
			run: func(o *ordering.Order) error { return o.Cancel("motivo") },
			apply: func(m *model) bool {
				if m.status == ordering.StatusShipped || m.status == ordering.StatusCancelled {
					return false
				}
				m.status = ordering.StatusCancelled
				return true
			},
		}
	}
}

// TestPropriedade_SequenciasAleatorias: com semente fixa, 400 sequências de
// 40 comandos aleatórios (válidos e inválidos). Depois de CADA comando:
// aceito ⇔ o oráculo aceita; estado observável = modelo; exatamente um evento
// se aceito e nenhum se rejeitado; total nunca passa do limite de crédito.
func TestPropriedade_SequenciasAleatorias(t *testing.T) {
	const seed = 36
	r := rand.New(rand.NewPCG(seed, 2026))
	for seq := 0; seq < 400; seq++ {
		limit := int64(5000 + r.IntN(60000))
		o, err := ordering.NewOrder(fmt.Sprintf("p-%d", seq), "c-1", brlc(t, limit))
		if err != nil {
			t.Fatalf("NewOrder: %v", err)
		}
		o.PullEvents()
		m := &model{status: ordering.StatusDraft, limit: limit}
		var history []string
		for step := 0; step < 40; step++ {
			c := randomCommand(r)
			history = append(history, c.desc)
			err := c.run(o)
			accepted := c.apply(m)
			evs := o.PullEvents()
			fail := func(format string, args ...any) {
				t.Helper()
				from := max(0, len(history)-12)
				t.Fatalf("semente %d, sequência %d, passo %d (limite %d centavos): %s\n  comandos: %s\n  pedido:   %v\n  esperado: %v",
					seed, seq, step, limit, fmt.Sprintf(format, args...),
					strings.Join(history[from:], " → "), view(o), m.view())
			}
			switch {
			case accepted && err != nil:
				fail("%s foi rejeitado (%v), mas é válido", c.desc, err)
			case !accepted && err == nil:
				fail("%s foi aceito, mas viola uma regra", c.desc)
			}
			names := eventNames(evs)
			if accepted && (len(names) != 1 || names[0] != c.event) {
				fail("%s aceito registrou %v; esperado [%s]", c.desc, names, c.event)
			}
			if !accepted && len(names) != 0 {
				fail("%s rejeitado registrou %v", c.desc, names)
			}
			if got, want := view(o), m.view(); !sameView(got, want) {
				fail("estado diverge do modelo depois de %s", c.desc)
			}
			if o.Total().Cents() > limit {
				fail("total %v acima do limite de crédito", o.Total())
			}
		}
	}
}
