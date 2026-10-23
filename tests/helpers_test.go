// Testes do avaliador do desafio-36. Usam SOMENTE a API pública documentada no
// README (construtores, métodos e erros sentinela); nenhum teste olha campos
// internos, então o participante é livre para organizar o código por dentro.
package tests

import (
	"errors"
	"fmt"
	"reflect"
	"strings"
	"testing"

	"desafio36/src/money"
	"desafio36/src/ordering"
)

const brl = "BRL"

// brlc cria um valor em reais a partir de centavos (falha o teste se inválido).
func brlc(t *testing.T, cents int64) money.Money {
	t.Helper()
	m, err := money.New(cents, brl)
	if err != nil {
		t.Fatalf("money.New(%d, BRL) devolveu erro: %v", cents, err)
	}
	return m
}

func usdc(t *testing.T, cents int64) money.Money {
	t.Helper()
	m, err := money.New(cents, "USD")
	if err != nil {
		t.Fatalf("money.New(%d, USD) devolveu erro: %v", cents, err)
	}
	return m
}

// newOrder abre um pedido com limite em BRL e descarta o evento OrderCreated.
func newOrder(t *testing.T, limitCents int64) *ordering.Order {
	t.Helper()
	o, err := ordering.NewOrder("pedido-1", "cliente-7", brlc(t, limitCents))
	if err != nil {
		t.Fatalf("NewOrder devolveu erro inesperado: %v", err)
	}
	if o == nil {
		t.Fatalf("NewOrder devolveu pedido nil sem erro")
	}
	o.PullEvents()
	return o
}

// must falha o teste se uma operação de preparação for rejeitada.
func must(t *testing.T, what string, err error) {
	t.Helper()
	if err != nil {
		t.Fatalf("preparação: %s foi rejeitado: %v", what, err)
	}
}

func eventNames(evs []ordering.Event) []string {
	names := make([]string, 0, len(evs))
	for _, e := range evs {
		if e == nil {
			names = append(names, "<nil>")
			continue
		}
		names = append(names, e.EventName())
	}
	return names
}

// lineView é a visão observável de um item, via métodos públicos.
type lineView struct {
	SKU      string
	Qty      int
	Unit     int64
	UnitCur  string
	Total    int64
	TotalCur string
}

// orderView é tudo que se observa de um pedido pela API pública.
type orderView struct {
	Status      ordering.Status
	Lines       []lineView
	Subtotal    int64
	Discount    int64
	Total       int64
	TotalCur    string
	SubtotalCur string
}

func view(o *ordering.Order) orderView {
	v := orderView{
		Status:      o.Status(),
		Subtotal:    o.Subtotal().Cents(),
		SubtotalCur: o.Subtotal().Currency(),
		Discount:    o.Discount().Cents(),
		Total:       o.Total().Cents(),
		TotalCur:    o.Total().Currency(),
	}
	for _, l := range o.Lines() {
		v.Lines = append(v.Lines, lineView{
			SKU: l.SKU(), Qty: l.Quantity(),
			Unit: l.UnitPrice().Cents(), UnitCur: l.UnitPrice().Currency(),
			Total: l.Total().Cents(), TotalCur: l.Total().Currency(),
		})
	}
	return v
}

func (v orderView) String() string {
	var b strings.Builder
	fmt.Fprintf(&b, "status=%s subtotal=%d desconto=%d total=%d %s itens=[", v.Status, v.Subtotal, v.Discount, v.Total, v.TotalCur)
	for i, l := range v.Lines {
		if i > 0 {
			b.WriteString(" ")
		}
		fmt.Fprintf(&b, "%s×%d@%d", l.SKU, l.Qty, l.Unit)
	}
	b.WriteString("]")
	return b.String()
}

func sameView(a, b orderView) bool { return reflect.DeepEqual(a, b) }

// isErr informa se err "é" want, aceitando erros embrulhados.
func isErr(err, want error) bool { return err != nil && errors.Is(err, want) }
