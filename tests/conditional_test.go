package tests

import (
	"testing"

	"desafio34/internal/store"
)

// TestCondicional_IfNoneMatch: o app consulta o produto a cada abertura da
// tela; se nada mudou, a resposta é 304 sem corpo.
func TestCondicional_IfNoneMatch(t *testing.T) {
	e := newEnv(t, store.Hooks{})
	p := e.store.CreateProduct(store.Product{Name: "Luminária", PriceCents: 12900, Stock: 8})
	path := "/products/" + p.ID

	r := e.do("GET", path, nil)
	tag := strongETag(t, r)

	cases := []struct{ name, header string }{
		{"If-None-Match com o ETag atual", tag},
		{"If-None-Match com uma lista que contém o ETag atual", `"outro", ` + tag},
		{"If-None-Match com o ETag atual marcado como fraco (comparação fraca)", "W/" + tag},
		{"If-None-Match: *", "*"},
	}
	for _, c := range cases {
		r := e.do("GET", path, nil, "If-None-Match", c.header)
		switch {
		case r.Status != 304:
			t.Errorf("%s: esperado 304 Not Modified\n        %s", c.name, r)
		case len(r.Body) != 0:
			t.Errorf("%s: 304 não pode ter corpo (veio %d bytes)", c.name, len(r.Body))
		case r.Header.Get("ETag") != tag:
			t.Errorf("%s: o 304 precisa repetir o ETag (%s), veio %q", c.name, tag, r.Header.Get("ETag"))
		}
	}

	if r := e.do("GET", path, nil, "If-None-Match", `"nao-e-este"`); r.Status != 200 || len(r.Body) == 0 {
		t.Errorf("If-None-Match com ETag diferente: esperado 200 com o produto\n        %s", r)
	}

	// Um pedido baixa o estoque: a representação mudou, o ETag também.
	if r := e.do("POST", "/orders", map[string]any{"product_id": p.ID, "quantity": 1, "qty": 1}); r.Status != 201 {
		t.Fatalf("POST /orders: esperado 201\n        %s", r)
	}
	r = e.do("GET", path, nil, "If-None-Match", tag)
	if r.Status != 200 {
		t.Fatalf("depois de um pedido (estoque 8 → 7), If-None-Match com o ETag antigo: esperado 200 com o produto novo\n        %s", r)
	}
	if o := object(t, r); o["stock"] != 7.0 {
		t.Errorf("esperado stock 7\n        %s", r)
	}
	tag2 := strongETag(t, r)
	if tag2 == tag {
		t.Errorf("o estoque mudou mas o ETag continuou %s", tag)
	}

	// Depois de um PUT, o ETag do PUT vale para o próximo GET condicional.
	r = e.do("PUT", path, productBody("Luminária", 9900, 7), "If-Match", tag2)
	if r.Status != 200 {
		t.Fatalf("PUT com If-Match atual: esperado 200\n        %s", r)
	}
	tag3 := strongETag(t, r)
	if r := e.do("GET", path, nil, "If-None-Match", tag2); r.Status != 200 {
		t.Errorf("depois do PUT, If-None-Match com o ETag anterior: esperado 200\n        %s", r)
	}
	if r := e.do("GET", path, nil, "If-None-Match", tag3); r.Status != 304 {
		t.Errorf("depois do PUT, If-None-Match com o ETag devolvido pelo PUT: esperado 304\n        %s", r)
	}
}
