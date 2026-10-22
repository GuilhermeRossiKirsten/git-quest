package tests

import (
	"sync"
	"testing"
	"time"
)

// Muitas leituras simultâneas do mesmo produto fora do cache (a vitrine da
// Black Friday abrindo) devem gerar UMA consulta ao banco por réplica — e sem
// atrasar as leituras de outros produtos.
func TestStampede_UmaIdaAoBanco(t *testing.T) {
	const n = 50
	e := newEnv(t, 5*time.Millisecond,
		product("acme", "vitrine", "Furadeira", 29990),
		product("acme", "outro", "Parafuso", 15))

	gate := e.db.HoldNextGet("acme", "vitrine")
	t.Cleanup(gate.Release)

	var ready sync.WaitGroup
	ready.Add(n)
	results := make(chan got, n)
	for i := 0; i < n; i++ {
		go func() {
			ready.Done()
			results <- read(e.svc, 15*time.Second, "acme", "vitrine")
		}()
	}
	ready.Wait()
	waitGate(t, gate, "nenhuma leitura chegou ao banco")

	// Com a primeira consulta presa no banco, outro produto continua rápido.
	select {
	case g := <-goRead(e, "acme", "outro"):
		if g.err != nil || g.p.Name != "Parafuso" {
			t.Errorf("leitura de outro produto durante a avalanche: (%+v, %v)", g.p, g.err)
		}
	case <-time.After(2 * time.Second):
		t.Errorf("a leitura de OUTRO produto ficou presa atrás da consulta lenta do produto da vitrine (> 2s)")
	}

	time.Sleep(300 * time.Millisecond) // tempo para as outras 49 leituras chegarem
	gate.Release()

	deadline := time.After(20 * time.Second)
	for i := 0; i < n; i++ {
		select {
		case g := <-results:
			if g.err != nil {
				t.Errorf("leitura concorrente falhou: %v", g.err)
			} else if g.p.Name != "Furadeira" || g.p.PriceCents != 29990 {
				t.Errorf("leitura concorrente devolveu %+v", g.p)
			}
		case <-deadline:
			t.Fatalf("só %d de %d leituras concorrentes terminaram em 20s — travou?", i, n)
		}
	}
	if calls := e.db.GetCalls("acme", "vitrine"); calls != 1 {
		t.Errorf("%d leituras simultâneas de um produto fora do cache foram %d vezes ao banco; esperado 1 (as outras deveriam esperar a primeira)", n, calls)
	}
}

func goRead(e *env, tenant, id string) <-chan got {
	ch := make(chan got, 1)
	go func() { ch <- read(e.svc, 5*time.Second, tenant, id) }()
	return ch
}
