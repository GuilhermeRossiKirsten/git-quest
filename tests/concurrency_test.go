package tests

import (
	"testing"

	"desafio34/internal/store"
)

func productBody(name string, cents int64, stock int) map[string]any {
	return map[string]any{"name": name, "price_cents": cents, "stock": stock}
}

// TestConcorrencia_IfMatch: duas pessoas editando o mesmo produto não podem
// apagar a alteração uma da outra sem saber (lost update).
func TestConcorrencia_IfMatch(t *testing.T) {
	g := newGate(t, store.OpReplaceProduct)
	e := newEnv(t, store.Hooks{BeforeWrite: g.hook})
	p := e.store.CreateProduct(store.Product{Name: "Cadeira", PriceCents: 50000, Stock: 10})
	path := "/products/" + p.ID

	// POST também devolve o ETag do recurso criado.
	if r := e.do("POST", "/products", productBody("Mesa", 90000, 2)); r.Status == 201 {
		strongETag(t, r)
	}

	// Sem precondição, a escrita é recusada: 428 Precondition Required.
	r := e.do("PUT", path, productBody("Cadeira", 1, 10))
	if msg := problemErr(r, 428); msg != "" {
		t.Errorf("PUT sem If-Match: %s", msg)
	}
	if cur, _ := e.store.GetProduct(p.ID); cur.PriceCents != 50000 {
		t.Fatalf("PUT sem If-Match alterou o produto (price_cents=%d)", cur.PriceCents)
	}

	// Ana (preço) e Beto (estoque) abrem o produto ao mesmo tempo.
	ana := e.do("GET", path, nil)
	beto := e.do("GET", path, nil)
	tag0 := strongETag(t, ana)
	if strongETag(t, beto) != tag0 {
		t.Fatalf("duas leituras do mesmo estado deram ETags diferentes")
	}

	r = e.do("PUT", path, productBody("Cadeira", 45000, 10), "If-Match", tag0)
	if r.Status != 200 {
		t.Fatalf("PUT da Ana com o ETag atual: esperado 200\n        %s", r)
	}
	tag1 := strongETag(t, r)
	if tag1 == tag0 {
		t.Fatalf("o ETag não mudou depois de uma alteração (%s)", tag1)
	}
	if g := e.do("GET", path, nil); g.Header.Get("ETag") != tag1 {
		t.Errorf("o ETag do PUT (%s) difere do ETag do GET seguinte (%s)", tag1, g.Header.Get("ETag"))
	}

	r = e.do("PUT", path, productBody("Cadeira", 50000, 3), "If-Match", tag0)
	if msg := problemErr(r, 412); msg != "" {
		t.Errorf("PUT do Beto com o ETag velho: %s", msg)
	}
	if cur, _ := e.store.GetProduct(p.ID); cur.PriceCents != 45000 {
		t.Fatalf("lost update: o PUT do Beto, feito sobre uma versão velha, apagou o preço da Ana (price_cents=%d, esperado 45000)", cur.PriceCents)
	}

	// Beto relê, reaplica a mudança dele e grava.
	fresh := strongETag(t, e.do("GET", path, nil))
	if r := e.do("PUT", path, productBody("Cadeira", 45000, 3), "If-Match", fresh); r.Status != 200 {
		t.Errorf("PUT do Beto com o ETag novo: esperado 200\n        %s", r)
	}
	if cur, _ := e.store.GetProduct(p.ID); cur.PriceCents != 45000 || cur.Stock != 3 {
		t.Errorf("estado final = preço %d / estoque %d, esperado 45000 / 3 (as duas mudanças)", cur.PriceCents, cur.Stock)
	}

	// Agora as duas escritas de verdade ao mesmo tempo, com o mesmo If-Match:
	// a primeira pausa DENTRO da gravação no store; a segunda corre no meio.
	tag := strongETag(t, e.do("GET", path, nil))
	ra, rb, _ := interleave(t, g,
		func() resp { return e.sendAsync("PUT", path, productBody("Cadeira", 11100, 3), "If-Match", tag) },
		func() resp { return e.sendAsync("PUT", path, productBody("Cadeira", 22200, 3), "If-Match", tag) },
	)
	cur, _ := e.store.GetProduct(p.ID)
	switch {
	case ra.Status == 200 && rb.Status == 200:
		t.Errorf("as duas escritas intercaladas com o mesmo If-Match foram aceitas (200 e 200): lost update — "+
			"verificar o ETag e depois gravar não é atômico (preço final %d)", cur.PriceCents)
	case ra.Status == 200 && rb.Status == 412:
		if cur.PriceCents != 11100 {
			t.Errorf("a 1ª escrita venceu, mas o preço final é %d (esperado 11100)", cur.PriceCents)
		}
	case ra.Status == 412 && rb.Status == 200:
		if cur.PriceCents != 22200 {
			t.Errorf("a 2ª escrita venceu, mas o preço final é %d (esperado 22200)", cur.PriceCents)
		}
	default:
		t.Errorf("escritas intercaladas: esperado uma 200 e outra 412\n        1ª: %s\n        2ª: %s", ra, rb)
	}
}
