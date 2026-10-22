package tests

import (
	"fmt"
	"strings"
	"testing"
	"time"
)

const (
	minProductTTL = 5 * time.Minute
	maxProductTTL = 15 * time.Minute
	maxNegTTL     = 60 * time.Second
	minSpread     = 100 * time.Second
)

// Produtos ficam no cache entre 5 e 15 minutos, mas as expirações de um lote
// carregado de uma vez não podem cair todas no mesmo instante: ninguém quer
// que 300 produtos voltem ao banco no mesmo segundo daqui a 10 minutos.
func TestTTL_ComJitter(t *testing.T) {
	const n = 300
	e := newEnv(t, 0)
	for i := 0; i < n; i++ {
		e.db.Seed(product("acme", fmt.Sprintf("p-%03d", i), "Produto", int64(100+i)))
	}
	start := time.Now()
	for i := 0; i < n; i++ {
		mustRead(t, e.svc, "réplica 1", "acme", fmt.Sprintf("p-%03d", i))
	}

	keys := e.redisKeys(t, start)
	var noTTL, outOfRange []string
	var products []keyTTL
	for _, k := range keys {
		switch {
		case k.pttl == -1:
			noTTL = append(noTTL, quote(k.key))
		case time.Duration(k.pttl)*time.Millisecond > maxNegTTL:
			products = append(products, k)
			if d := time.Duration(k.pttl) * time.Millisecond; d < minProductTTL-30*time.Second || d > maxProductTTL {
				outOfRange = append(outOfRange, fmt.Sprintf("%s (%s)", quote(k.key), ms(k.pttl)))
			}
		}
	}
	if len(noTTL) > 0 {
		t.Errorf("%d chave(s) sem expiração (TTL -1), ex.: %s", len(noTTL), strings.Join(first(noTTL, 3), ", "))
	}
	if len(products) < n {
		t.Fatalf("depois de ler %d produtos, só %d chave(s) no Redis têm TTL > %v; esperado ao menos %d", n, len(products), maxNegTTL, n)
	}
	if len(outOfRange) > 0 {
		t.Errorf("%d produto(s) com TTL fora de [%v, %v], ex.: %s", len(outOfRange), minProductTTL, maxProductTTL, strings.Join(first(outOfRange, 3), ", "))
	}

	// Em que segundo cada entrada expira?
	perSecond := map[int64]int{}
	lo, hi := products[0].expireAt, products[0].expireAt
	for _, k := range products {
		perSecond[int64(k.expireAt/time.Second)]++
		lo, hi = min(lo, k.expireAt), max(hi, k.expireAt)
	}
	worst, worstSec := 0, int64(0)
	for s, c := range perSecond {
		if c > worst {
			worst, worstSec = c, s
		}
	}
	limit := len(products) / 20 // 5%
	if worst > limit {
		t.Errorf("%d de %d produtos expiram no mesmo segundo (daqui a ~%ds); o máximo aceito é %d (5%%) — sem dispersão, todos voltam ao banco juntos",
			worst, len(products), worstSec, limit)
	}
	if spread := hi - lo; spread < minSpread {
		t.Errorf("as expirações de %d produtos carregados juntos se espalham por só %v; esperado ao menos %v",
			len(products), spread.Round(time.Second), minSpread)
	}
}

// Consultas a IDs que não existem (robôs, links velhos) também são servidas
// pelo cache — por pouco tempo, para um produto recém-criado aparecer logo.
func TestNegativo_IdInexistente(t *testing.T) {
	const ids, reads = 20, 5
	e := newEnv(t, 5*time.Millisecond, product("acme", "1", "Martelo", 1000))

	id := func(i int) string { return fmt.Sprintf("fantasma-%02d", i) }
	for i := 0; i < ids; i++ {
		for j := 0; j < reads; j++ {
			g, ok := within(10*time.Second, func() got { return read(e.svc, 5*time.Second, "acme", id(i)) })
			if !ok {
				t.Fatalf("GetProduct de um ID inexistente não retornou em 10s")
			}
			if !isNotFound(g.err) {
				t.Fatalf("GetProduct(%q) (inexistente), leitura %d: devolveu (%+v, %v); esperado errors.Is(err, productdb.ErrNotFound)",
					id(i), j+1, g.p, g.err)
			}
		}
	}
	for i := 0; i < ids; i++ {
		g := read(e.svc2, 5*time.Second, "acme", id(i))
		if !isNotFound(g.err) {
			t.Fatalf("réplica 2, GetProduct(%q) (inexistente): devolveu (%+v, %v); esperado ErrNotFound", id(i), g.p, g.err)
		}
	}
	total := 0
	for i := 0; i < ids; i++ {
		total += e.db.GetCalls("acme", id(i))
	}
	if total > ids {
		t.Errorf("%d IDs inexistentes, lidos %d vezes cada (em duas réplicas), foram %d vezes ao banco; esperado no máximo %d (uma por ID)",
			ids, reads+1, total, ids)
	}

	var bad []string
	for _, k := range e.redisKeys(t, time.Now()) {
		if k.pttl == -1 || time.Duration(k.pttl)*time.Millisecond > maxNegTTL {
			bad = append(bad, fmt.Sprintf("%s (TTL %s)", quote(k.key), ms(k.pttl)))
		}
	}
	if len(bad) > 0 {
		t.Errorf("entradas de produtos inexistentes devem expirar em até %v; %d chave(s) não, ex.: %s",
			maxNegTTL, len(bad), strings.Join(first(bad, 3), ", "))
	}

	// O produto passa a existir: aparece na hora, nas duas réplicas.
	mustSave(t, e.svc, product("acme", id(3), "Novo produto", 4990))
	for _, s := range []struct {
		who string
		i   int
	}{{"réplica 1", 1}, {"réplica 2", 2}} {
		svc := e.svc
		if s.i == 2 {
			svc = e.svc2
		}
		g := read(svc, 5*time.Second, "acme", id(3))
		if g.err != nil || g.p.Name != "Novo produto" {
			t.Errorf("%s: depois de SaveProduct criar %q, GetProduct devolveu (%+v, %v); esperado o produto novo",
				s.who, id(3), g.p, g.err)
		}
	}
}

func first(s []string, n int) []string {
	if len(s) > n {
		return s[:n]
	}
	return s
}
