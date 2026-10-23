package tests

import (
	"encoding/json"
	"reflect"
	"testing"

	"desafio34/internal/store"
)

func sameJSON(a, b []byte) bool {
	var x, y any
	return json.Unmarshal(a, &x) == nil && json.Unmarshal(b, &y) == nil && reflect.DeepEqual(x, y)
}

// TestIdempotencia_Pedidos: POST /orders com Idempotency-Key pode ser repetido
// com segurança.
func TestIdempotencia_Pedidos(t *testing.T) {
	g := newGate(t, store.OpCreateOrder)
	e := newEnv(t, store.Hooks{BeforeWrite: g.hook})
	p := e.store.CreateProduct(store.Product{Name: "Parafusadeira", PriceCents: 30000, Stock: 100})
	body := map[string]any{"product_id": p.ID, "quantity": 2, "qty": 2}

	first := e.do("POST", "/orders", body, "Idempotency-Key", "app-7f3a")
	if first.Status != 201 {
		t.Fatalf("1º POST /orders com Idempotency-Key: esperado 201\n        %s", first)
	}
	replay := e.do("POST", "/orders", body, "Idempotency-Key", "app-7f3a")
	switch {
	case replay.Status != first.Status:
		t.Errorf("repetição com a mesma chave: status %d, esperado o mesmo da 1ª (%d)\n        %s", replay.Status, first.Status, replay)
	case !sameJSON(first.Body, replay.Body):
		t.Errorf("repetição com a mesma chave devolveu outro corpo:\n        1ª: %s\n        2ª: %s", first, replay)
	case replay.Header.Get("Location") != first.Header.Get("Location"):
		t.Errorf("repetição com a mesma chave: Location %q, esperado %q", replay.Header.Get("Location"), first.Header.Get("Location"))
	}
	if n := len(e.store.Orders()); n != 1 {
		t.Errorf("depois de repetir o POST com a mesma chave existem %d pedidos, esperado 1", n)
	}
	if cur, _ := e.store.GetProduct(p.ID); cur.Stock != 98 {
		t.Errorf("estoque = %d, esperado 98 (uma única baixa)", cur.Stock)
	}

	// Mesma chave, outro pedido: é um erro do cliente, não um replay.
	other := map[string]any{"product_id": p.ID, "quantity": 5, "qty": 5}
	r := e.do("POST", "/orders", other, "Idempotency-Key", "app-7f3a")
	if msg := problemErr(r, 422); msg != "" {
		t.Errorf("mesma Idempotency-Key com corpo diferente: %s", msg)
	}
	if n := len(e.store.Orders()); n != 1 {
		t.Errorf("a chave reutilizada com outro corpo criou um pedido (agora são %d)", n)
	}

	// Chave nova, mesmo corpo: é OUTRO pedido legítimo.
	r = e.do("POST", "/orders", body, "Idempotency-Key", "app-8c1d")
	if r.Status != 201 || sameJSON(r.Body, first.Body) {
		t.Errorf("outra chave com o mesmo corpo deveria criar outro pedido (201, id novo)\n        %s", r)
	}
	// Sem chave nenhuma: cada POST cria um pedido, como sempre.
	for range 2 {
		if r := e.do("POST", "/orders", body); r.Status != 201 {
			t.Errorf("POST /orders sem Idempotency-Key: esperado 201\n        %s", r)
		}
	}
	if n := len(e.store.Orders()); n != 4 {
		t.Errorf("esperado 4 pedidos até aqui, há %d", n)
	}

	// Duas cópias da mesma requisição chegando juntas (retry agressivo): a 1ª
	// pausa dentro da gravação no store e a 2ª chega no meio.
	before := len(e.store.Orders())
	send := func() resp { return e.sendAsync("POST", "/orders", body, "Idempotency-Key", "app-9e02") }
	ra, rb, bEarly := interleave(t, g, send, send)
	after := len(e.store.Orders())
	if after-before != 1 {
		t.Errorf("duas requisições simultâneas com a mesma chave criaram %d pedidos, esperado 1 "+
			"(a chave precisa ser reservada antes de criar o pedido)\n        1ª: %s\n        2ª: %s", after-before, ra, rb)
	}
	if ra.Status != 201 {
		t.Errorf("a 1ª requisição simultânea: esperado 201\n        %s", ra)
	}
	switch {
	case rb.Status == 409:
		if msg := problemErr(rb, 409); msg != "" {
			t.Errorf("a 2ª requisição simultânea (em andamento): %s", msg)
		}
	case rb.Status == 201 && !bEarly:
		if !sameJSON(ra.Body, rb.Body) {
			t.Errorf("a 2ª requisição simultânea esperou e devolveu outro pedido:\n        1ª: %s\n        2ª: %s", ra, rb)
		}
	default:
		t.Errorf("a 2ª requisição simultânea: esperado 409 (em andamento) ou, depois de esperar a 1ª, o mesmo 201\n        %s", rb)
	}
}
