package tests

import (
	"testing"

	"desafio34/internal/store"
)

// TestCaminhoFeliz: o fluxo básico do app novo (já funciona no código inicial
// e precisa continuar funcionando).
func TestCaminhoFeliz(t *testing.T) {
	e := newEnv(t, store.Hooks{})

	r := e.do("POST", "/products", map[string]any{"name": "Caneca", "price_cents": 3990, "stock": 5})
	if r.Status != 201 {
		t.Fatalf("POST /products: esperado 201\n        %s", r)
	}
	if mediaType(r) != "application/json" {
		t.Errorf("POST /products: Content-Type esperado application/json\n        %s", r)
	}
	p := object(t, r)
	id, _ := p["id"].(string)
	if id == "" || p["name"] != "Caneca" || p["price_cents"] != 3990.0 || p["stock"] != 5.0 {
		t.Fatalf("POST /products devolveu um produto inesperado\n        %s", r)
	}
	if loc := r.Header.Get("Location"); loc != "/products/"+id {
		t.Errorf("Location = %q, esperado /products/%s", loc, id)
	}

	if r := e.do("GET", "/products/"+id, nil); r.Status != 200 || object(t, r)["name"] != "Caneca" {
		t.Errorf("GET /products/%s: esperado 200 com o produto\n        %s", id, r)
	}

	r = e.do("GET", "/products?limit=10", nil)
	page := object(t, r)
	items, _ := page["items"].([]any)
	if r.Status != 200 || len(items) != 1 {
		t.Errorf("GET /products?limit=10: esperado 200 com 1 item\n        %s", r)
	}

	// O app novo manda "qty"; o contrato documentado é "quantity". Mandamos os dois.
	r = e.do("POST", "/orders", map[string]any{"product_id": id, "quantity": 2, "qty": 2})
	if r.Status != 201 {
		t.Fatalf("POST /orders: esperado 201\n        %s", r)
	}
	o := object(t, r)
	oid, _ := o["id"].(string)
	if oid == "" || o["total_cents"] != 7980.0 || o["status"] != "confirmed" {
		t.Errorf("POST /orders devolveu um pedido inesperado (esperado total_cents 7980, status confirmed)\n        %s", r)
	}
	if r := e.do("GET", "/orders/"+oid, nil); r.Status != 200 {
		t.Errorf("GET /orders/%s: esperado 200\n        %s", oid, r)
	}
	if got, _ := e.store.GetProduct(id); got.Stock != 3 {
		t.Errorf("estoque depois do pedido = %d, esperado 3", got.Stock)
	}

	if r := e.do("DELETE", "/products/"+id, nil); r.Status != 204 {
		t.Errorf("DELETE /products/%s: esperado 204\n        %s", id, r)
	}
	if r := e.do("GET", "/products/"+id, nil); r.Status != 404 {
		t.Errorf("GET de produto removido: esperado 404\n        %s", r)
	}
}
