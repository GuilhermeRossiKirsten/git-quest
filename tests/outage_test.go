package tests

import (
	"context"
	"net"
	"sync"
	"testing"
	"time"

	"gitquest/catalog/internal/catalog"
	"gitquest/catalog/internal/productdb"
	"gitquest/catalog/internal/redisx"
)

const outageBudget = time.Second

// O cache é uma otimização: se o Redis cai (ou para de responder), o catálogo
// continua no ar servindo do banco, sem travar. Os testes chamam o serviço
// com um prazo generoso (3s) e exigem resposta em até 1s.
func TestRedisFora_Degrada(t *testing.T) {
	t.Run("conexão aceita, mas o Redis não responde", func(t *testing.T) {
		checkOutage(t, blackHole(t))
	})
	t.Run("conexão recusada", func(t *testing.T) {
		ln, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			t.Fatal(err)
		}
		addr := ln.Addr().String()
		_ = ln.Close()
		checkOutage(t, addr)
	})
}

func checkOutage(t *testing.T, addr string) {
	db := productdb.New(5 * time.Millisecond)
	db.Seed(product("acme", "1", "Martelo", 1000))
	cache := redisx.New(addr, 0)
	t.Cleanup(func() { _ = cache.Close() })
	svc := catalog.New(cache, db)

	for i := 1; i <= 3; i++ {
		g, ok := within(10*time.Second, func() got { return read(svc, 3*time.Second, "acme", "1") })
		switch {
		case !ok:
			t.Fatalf("leitura %d: GetProduct não retornou nem em 10s (ignorou o prazo do ctx?)", i)
		case g.err != nil:
			t.Fatalf("leitura %d: GetProduct falhou em %v em vez de servir do banco: %v", i, g.took.Round(time.Millisecond), g.err)
		case g.p.Name != "Martelo" || g.p.PriceCents != 1000:
			t.Fatalf("leitura %d: GetProduct devolveu %+v", i, g.p)
		case g.took > outageBudget:
			t.Fatalf("leitura %d: GetProduct demorou %v com o Redis fora; esperado até %v", i, g.took.Round(time.Millisecond), outageBudget)
		}
	}

	type res struct {
		err  error
		took time.Duration
	}
	r, ok := within(10*time.Second, func() res {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		start := time.Now()
		err := svc.SaveProduct(ctx, product("acme", "1", "Martelo", 2000))
		return res{err, time.Since(start)}
	})
	if !ok {
		t.Fatal("SaveProduct não retornou nem em 10s com o Redis fora")
	}
	if p, _ := db.Peek("acme", "1"); p.PriceCents != 2000 {
		t.Fatalf("com o Redis fora, SaveProduct não gravou no banco (preço no banco: %d; erro: %v) — o banco é a fonte da verdade", p.PriceCents, r.err)
	}
	if r.took > outageBudget {
		t.Fatalf("SaveProduct demorou %v com o Redis fora; esperado até %v", r.took.Round(time.Millisecond), outageBudget)
	}
}

// blackHole aceita conexões TCP e nunca responde — como um Redis travado ou
// uma rede que descarta pacotes.
func blackHole(t *testing.T) string {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	var mu sync.Mutex
	var conns []net.Conn
	done := make(chan struct{})
	go func() {
		defer close(done)
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			mu.Lock()
			conns = append(conns, c)
			mu.Unlock()
		}
	}()
	t.Cleanup(func() {
		_ = ln.Close()
		<-done
		mu.Lock()
		defer mu.Unlock()
		for _, c := range conns {
			_ = c.Close()
		}
	})
	return ln.Addr().String()
}
