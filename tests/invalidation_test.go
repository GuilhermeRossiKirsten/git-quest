package tests

import (
	"context"
	"testing"
	"time"
)

// Depois que SaveProduct retorna, toda leitura — desta ou de outra réplica —
// vê o produto novo. Inclusive quando alguém leu o produto ENQUANTO a
// gravação estava em andamento no banco.
func TestInvalidacao_LeituraDuranteEscrita(t *testing.T) {
	e := newEnv(t, 5*time.Millisecond, product("acme", "1", "Martelo", 1000))

	// Caso simples, sem concorrência.
	mustRead(t, e.svc, "réplica 1", "acme", "1")
	mustRead(t, e.svc2, "réplica 2", "acme", "1")
	mustSave(t, e.svc, product("acme", "1", "Martelo", 1500))
	for _, r := range []struct {
		who string
		i   int
	}{{"réplica 1", 1}, {"réplica 2", 2}} {
		svc := e.svc
		if r.i == 2 {
			svc = e.svc2
		}
		if p := mustRead(t, svc, r.who, "acme", "1"); p.PriceCents != 1500 {
			t.Fatalf("sem concorrência: depois de SaveProduct(preço 1500), a %s ainda lê %d", r.who, p.PriceCents)
		}
	}

	// Uma leitura chega durante a gravação: o banco ainda tem o preço antigo.
	gate := e.db.HoldNextSave("acme", "1")
	t.Cleanup(gate.Release)
	saved := make(chan error, 1)
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		saved <- e.svc.SaveProduct(ctx, product("acme", "1", "Martelo", 2000))
	}()
	waitGate(t, gate, "SaveProduct não chegou a gravar no banco")

	during, ok := within(10*time.Second, func() got { return read(e.svc2, 5*time.Second, "acme", "1") })
	if !ok || during.err != nil {
		t.Fatalf("a leitura feita durante a gravação falhou ou travou: %v", during.err)
	}
	if during.p.PriceCents != 1500 && during.p.PriceCents != 2000 {
		t.Fatalf("a leitura durante a gravação devolveu o preço %d (nem o antigo 1500 nem o novo 2000)", during.p.PriceCents)
	}

	gate.Release()
	select {
	case err := <-saved:
		if err != nil {
			t.Fatalf("SaveProduct(preço 2000): %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("SaveProduct não retornou em 10s depois de o banco ser liberado")
	}

	p1 := mustRead(t, e.svc, "réplica 1", "acme", "1")
	p2 := mustRead(t, e.svc2, "réplica 2", "acme", "1")
	if p1.PriceCents != 2000 || p2.PriceCents != 2000 {
		t.Errorf("uma leitura feita DURANTE a gravação deixou o preço antigo no cache: depois de SaveProduct(preço 2000) retornar, a réplica 1 lê %d e a réplica 2 lê %d",
			p1.PriceCents, p2.PriceCents)
	}
}

// A corrida clássica do cache-aside: uma leitura lenta busca o preço antigo
// no banco; enquanto ela está "no ar", o preço muda e o cache é invalidado;
// então a leitura lenta termina e grava o valor velho no cache. Depois que
// SaveProduct retornou e a leitura lenta terminou, ninguém pode ver o preço
// antigo.
func TestInvalidacao_CorridaLeituraLenta(t *testing.T) {
	e := newEnv(t, 5*time.Millisecond, product("acme", "1", "Martelo", 1000))

	gate := e.db.HoldNextGet("acme", "1")
	t.Cleanup(gate.Release)
	slow := make(chan got, 1)
	go func() { slow <- read(e.svc, 10*time.Second, "acme", "1") }()
	waitGate(t, gate, "a leitura com o cache vazio não foi ao banco")

	// O banco já devolveu 1000 para a leitura lenta, que ainda não terminou.
	err, ok := within(10*time.Second, func() error {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		return e.svc2.SaveProduct(ctx, product("acme", "1", "Martelo", 2000))
	})
	if !ok {
		t.Fatal("SaveProduct travou esperando a leitura lenta terminar — a escrita não pode depender de leituras em andamento")
	}
	if err != nil {
		t.Fatalf("SaveProduct(preço 2000): %v", err)
	}

	gate.Release()
	var r got
	select {
	case r = <-slow:
	case <-time.After(10 * time.Second):
		t.Fatal("a leitura lenta não terminou em 10s depois de liberada")
	}
	if r.err != nil {
		t.Fatalf("a leitura lenta falhou: %v", r.err)
	}
	if r.p.PriceCents != 1000 && r.p.PriceCents != 2000 {
		t.Fatalf("a leitura lenta devolveu o preço %d (nem 1000 nem 2000)", r.p.PriceCents)
	}

	p1 := mustRead(t, e.svc, "réplica 1", "acme", "1")
	p2 := mustRead(t, e.svc2, "réplica 2", "acme", "1")
	if p1.PriceCents != 2000 || p2.PriceCents != 2000 {
		t.Errorf("a leitura lenta repovoou o cache com o preço antigo depois da invalidação: com SaveProduct(preço 2000) já concluído, a réplica 1 lê %d e a réplica 2 lê %d",
			p1.PriceCents, p2.PriceCents)
	}
}
