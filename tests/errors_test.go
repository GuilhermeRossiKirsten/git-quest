package tests

import (
	"testing"

	"desafio34/internal/store"
)

// TestErros_ProblemJSON: todo erro é um application/problem+json (RFC 9457)
// com o código HTTP certo e sem vazar detalhes internos.
func TestErros_ProblemJSON(t *testing.T) {
	e := newEnv(t, store.Hooks{})
	p := e.store.CreateProduct(store.Product{Name: "Banqueta", PriceCents: 15000, Stock: 2})
	tag := e.do("GET", "/products/"+p.ID, nil).Header.Get("ETag")

	cases := []struct {
		name         string
		method, path string
		body         any
		hdr          []string
		want         []int
	}{
		{"produto inexistente", "GET", "/products/prd_9999", nil, nil, []int{404}},
		{"remover produto inexistente", "DELETE", "/products/prd_9999", nil, nil, []int{404}},
		{"pedido inexistente", "GET", "/orders/ord_9999", nil, nil, []int{404}},
		{"limit=0", "GET", "/products?limit=0", nil, nil, []int{400}},
		{"limit negativo", "GET", "/products?limit=-1", nil, nil, []int{400}},
		{"limit não numérico", "GET", "/products?limit=abc", nil, nil, []int{400}},
		{"limit acima de 100", "GET", "/products?limit=101", nil, nil, []int{400}},
		{"cursor forjado", "GET", "/products?cursor=%40%40nao-e-um-cursor", nil, nil, []int{400}},
		{"JSON malformado", "POST", "/products", `{"name": "Mesa",`, nil, []int{400}},
		{"campo com tipo errado", "POST", "/products", `{"name": "Mesa", "price_cents": "barato", "stock": 1}`, nil, []int{400, 422}},
		{"produto sem nome", "POST", "/products", productBody("", 100, 1), nil, []int{422}},
		{"preço negativo", "POST", "/products", productBody("Mesa", -1, 1), nil, []int{422}},
		{"PUT com nome vazio", "PUT", "/products/" + p.ID, productBody("", 100, 1), []string{"If-Match", tag}, []int{422}},
		{"pedido com quantity 0", "POST", "/orders", map[string]any{"product_id": p.ID, "quantity": 0}, nil, []int{422}},
		{"pedido de produto inexistente", "POST", "/orders", map[string]any{"product_id": "prd_9999", "quantity": 1}, nil, []int{422, 404}},
		{"pedido acima do estoque", "POST", "/orders", map[string]any{"product_id": p.ID, "quantity": 3}, nil, []int{409}},
		{"PUT sem If-Match", "PUT", "/products/" + p.ID, productBody("Banqueta", 100, 1), nil, []int{428}},
		{"PUT com If-Match velho", "PUT", "/products/" + p.ID, productBody("Banqueta", 100, 1), []string{"If-Match", `"versao-velha"`}, []int{412}},
	}
	for _, c := range cases {
		r := e.do(c.method, c.path, c.body, c.hdr...)
		if msg := problemErr(r, c.want...); msg != "" {
			t.Errorf("%s: %s", c.name, msg)
		}
	}
	if cur, _ := e.store.GetProduct(p.ID); cur.PriceCents != 15000 || cur.Stock != 2 || len(e.store.Orders()) != 0 {
		t.Errorf("requisições inválidas alteraram o estado: %+v, %d pedidos", cur, len(e.store.Orders()))
	}
}
