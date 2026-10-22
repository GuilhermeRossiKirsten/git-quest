package tests

import (
	"testing"
	"time"
)

// O cache precisa existir de verdade: leituras repetidas — inclusive de outra
// réplica — não voltam ao banco, e o valor fica no Redis (não só na memória
// de um processo).
func TestCache_CompartilhadoEntreReplicas(t *testing.T) {
	e := newEnv(t, 20*time.Millisecond, product("acme", "1", "Martelo", 1990))

	for i := 0; i < 5; i++ {
		if p := mustRead(t, e.svc, "réplica 1", "acme", "1"); p.Name != "Martelo" || p.PriceCents != 1990 {
			t.Fatalf("réplica 1 leu %+v; esperado Martelo a 1990", p)
		}
	}
	for i := 0; i < 5; i++ {
		if p := mustRead(t, e.svc2, "réplica 2", "acme", "1"); p.Name != "Martelo" || p.PriceCents != 1990 {
			t.Fatalf("réplica 2 leu %+v; esperado Martelo a 1990", p)
		}
	}
	if n := e.db.GetCalls("acme", "1"); n != 1 {
		t.Errorf("10 leituras do mesmo produto (5 em cada réplica) foram %d vez(es) ao banco; esperado 1 — o cache não está sendo usado ou não é compartilhado pelas réplicas", n)
	}
	if keys := e.redisKeys(t, time.Now()); len(keys) == 0 {
		t.Errorf("nenhuma chave no Redis depois das leituras — o produto deveria estar no cache compartilhado")
	}

	g := read(e.svc, 5*time.Second, "acme", "nao-existe")
	if !isNotFound(g.err) {
		t.Errorf("GetProduct de um ID inexistente devolveu (%+v, %v); esperado um erro que satisfaça errors.Is(err, productdb.ErrNotFound)", g.p, g.err)
	}
}

// Lojas diferentes podem ter o mesmo ID de produto, e lojas e IDs são texto
// livre (podem conter ':', espaços, chaves...). Cada leitura tem de devolver o
// produto daquela loja, venha ele do banco ou do cache.
func TestChaves_SemColisao(t *testing.T) {
	ps := []struct{ tenant, id, name string }{
		{"acme", "42", "Martelo"},
		{"globex", "42", "Bigorna"},
		{"loja:sp", "7", "Serrote"},
		{"loja", "sp:7", "Alicate"},
		{"loja:sp:7", "", "Produto sem ID"},
		{"acme", "kit {a} b", "Kit de pregos"},
	}
	e := newEnv(t, 5*time.Millisecond)
	for i, p := range ps {
		e.db.Seed(product(p.tenant, p.id, p.name, int64(100*(i+1))))
	}

	check := func(who string, svcIdx int, order []int) {
		svc := e.svc
		if svcIdx == 2 {
			svc = e.svc2
		}
		for _, i := range order {
			p := ps[i]
			g, ok := within(10*time.Second, func() got { return read(svc, 5*time.Second, p.tenant, p.id) })
			switch {
			case !ok:
				t.Fatalf("%s: GetProduct(%q, %q) não retornou em 10s", who, p.tenant, p.id)
			case g.err != nil:
				t.Errorf("%s: GetProduct(%q, %q) falhou: %v", who, p.tenant, p.id, g.err)
			case g.p.Tenant != p.tenant || g.p.ID != p.id || g.p.Name != p.name:
				t.Errorf("%s: GetProduct(%q, %q) devolveu %q da loja %q (ID %q) — esperado %q",
					who, p.tenant, p.id, g.p.Name, g.p.Tenant, g.p.ID, p.name)
			}
		}
	}
	check("réplica 1, 1ª passada", 1, []int{0, 1, 2, 3, 4, 5})
	check("réplica 1, 2ª passada (do cache)", 1, []int{5, 4, 3, 2, 1, 0})
	check("réplica 2", 2, []int{1, 0, 3, 2, 5, 4})
}
