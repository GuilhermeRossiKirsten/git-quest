package tests

import (
	"encoding/json"
	"fmt"
	"slices"
	"strings"
	"testing"

	"desafio34/internal/store"
)

type pageResult struct {
	ids  []string
	next *string
	raw  resp
}

func (e *env) page(t *testing.T, path string) pageResult {
	t.Helper()
	r := e.do("GET", path, nil)
	if r.Status != 200 {
		t.Fatalf("GET %s: esperado 200\n        %s", path, r)
	}
	var body struct {
		Items []struct {
			ID string `json:"id"`
		} `json:"items"`
		Next *string `json:"next"`
	}
	if err := json.Unmarshal(r.Body, &body); err != nil {
		t.Fatalf("GET %s: corpo ilegível: %v\n        %s", path, err, r)
	}
	pr := pageResult{next: body.Next, raw: r}
	for _, it := range body.Items {
		pr.ids = append(pr.ids, it.ID)
	}
	return pr
}

// TestPaginacao_Cursor: percorrer o catálogo enquanto ele muda não pode
// repetir nem pular produtos.
func TestPaginacao_Cursor(t *testing.T) {
	e := newEnv(t, store.Hooks{})
	seeded := e.seed(100, 200, 300, 400, 500, 600, 700, 800, 900, 1000)
	// A listagem é do mais novo para o mais antigo: P10, P9, …, P1.
	var want []string
	for i := len(seeded) - 1; i >= 0; i-- {
		want = append(want, seeded[i].ID)
	}

	p1 := e.page(t, "/products?limit=3")
	if !slices.Equal(p1.ids, want[:3]) {
		t.Fatalf("1ª página = %v, esperado %v (mais novos primeiro, limit=3)", p1.ids, want[:3])
	}
	if p1.next == nil {
		t.Fatalf("1ª página sem link next\n        %s", p1.raw)
	}
	if strings.Contains(*p1.next, "offset=") {
		t.Errorf("o link next (%s) ainda é por posição (offset): ele não sobrevive a inserções e remoções", *p1.next)
	}

	// Enquanto o ERP lê a 1ª página, a loja cadastra 3 produtos novos.
	for i := range 3 {
		if r := e.do("POST", "/products", map[string]any{"name": fmt.Sprintf("Novo %d", i), "price_cents": 990, "stock": 1}); r.Status != 201 {
			t.Fatalf("POST /products: esperado 201\n        %s", r)
		}
	}

	p2 := e.page(t, *p1.next)
	if !slices.Equal(p2.ids, want[3:6]) {
		t.Errorf("2ª página, depois de 3 inserções = %v, esperado %v — o cliente veria produtos repetidos ou pularia produtos", p2.ids, want[3:6])
	}
	if again := e.page(t, *p1.next); !slices.Equal(again.ids, p2.ids) {
		t.Errorf("o mesmo link next devolveu páginas diferentes: %v e depois %v", p2.ids, again.ids)
	}
	if p2.next == nil {
		t.Fatalf("2ª página sem link next\n        %s", p2.raw)
	}

	// Agora some um produto já visto (P9) e o último da 2ª página (P5), para o
	// qual o cursor aponta.
	for _, id := range []string{want[1], want[5]} {
		if r := e.do("DELETE", "/products/"+id, nil); r.Status != 204 {
			t.Fatalf("DELETE /products/%s: esperado 204\n        %s", id, r)
		}
	}

	seen := append(slices.Clone(p1.ids), p2.ids...)
	next := p2.next
	for n := 3; next != nil; n++ {
		if n > 10 {
			t.Fatal("mais de 10 páginas: o link next nunca termina")
		}
		p := e.page(t, *next)
		if len(p.ids) > 3 {
			t.Errorf("página %d tem %d itens, acima de limit=3", n, len(p.ids))
		}
		seen = append(seen, p.ids...)
		next = p.next
	}

	if !slices.Equal(seen, want) {
		t.Errorf("travessia completa = %v\n        esperado          %v\n        (cada produto que existia do início ao fim exatamente uma vez, na ordem)", seen, want)
	}

	// Uma página que cabe inteira: next precisa ser null (o cliente v1 para aí).
	if p := e.page(t, "/products?limit=100"); p.next != nil {
		t.Errorf("com limit=100 (todos os itens cabem), next deveria ser null, veio %q", *p.next)
	}
}
