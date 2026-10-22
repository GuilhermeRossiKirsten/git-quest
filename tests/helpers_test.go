package tests

import (
	"context"
	"errors"
	"fmt"
	"os"
	"sort"
	"testing"
	"time"

	"gitquest/catalog/internal/catalog"
	"gitquest/catalog/internal/productdb"
	"gitquest/catalog/internal/redisx"
)

// env é um cenário: um banco simulado, o Redis do compose (banco lógico 0,
// esvaziado no início de cada teste) e duas réplicas do serviço, cada uma com
// o seu próprio cliente Redis.
type env struct {
	db    *productdb.DB
	admin *redisx.Client // usado só pelos testes, para inspecionar o Redis
	svc   *catalog.Service
	svc2  *catalog.Service
}

func newEnv(t *testing.T, latency time.Duration, seed ...productdb.Product) *env {
	t.Helper()
	addr := os.Getenv("REDIS_ADDR")
	if addr == "" {
		t.Fatal("REDIS_ADDR não definida — rode os testes com ./test.sh")
	}
	admin := redisx.New(addr, 0)
	t.Cleanup(func() { _ = admin.Close() })
	ctx := testCtx(t, 5*time.Second)
	if err := admin.Ping(ctx); err != nil {
		t.Fatalf("Redis indisponível em %s: %v — rode ./start.sh", addr, err)
	}
	if err := admin.FlushDB(ctx); err != nil {
		t.Fatalf("FLUSHDB: %v", err)
	}
	db := productdb.New(latency)
	db.Seed(seed...)
	c1, c2 := redisx.New(addr, 0), redisx.New(addr, 0)
	t.Cleanup(func() { _ = c1.Close(); _ = c2.Close() })
	return &env{db: db, admin: admin, svc: catalog.New(c1, db), svc2: catalog.New(c2, db)}
}

func testCtx(t *testing.T, d time.Duration) context.Context {
	ctx, cancel := context.WithTimeout(context.Background(), d)
	t.Cleanup(cancel)
	return ctx
}

func product(tenant, id, name string, price int64) productdb.Product {
	return productdb.Product{Tenant: tenant, ID: id, Name: name, PriceCents: price}
}

// got é o resultado de uma chamada a GetProduct.
type got struct {
	p    productdb.Product
	err  error
	took time.Duration
}

// read chama GetProduct com um ctx de prazo d. Pode rodar em qualquer goroutine.
func read(svc *catalog.Service, d time.Duration, tenant, id string) got {
	ctx, cancel := context.WithTimeout(context.Background(), d)
	defer cancel()
	start := time.Now()
	p, err := svc.GetProduct(ctx, tenant, id)
	return got{p: p, err: err, took: time.Since(start)}
}

// mustRead lê e exige sucesso; o teste falha (sem travar) se a chamada não voltar.
func mustRead(t *testing.T, svc *catalog.Service, who, tenant, id string) productdb.Product {
	t.Helper()
	g, ok := within(10*time.Second, func() got { return read(svc, 5*time.Second, tenant, id) })
	if !ok {
		t.Fatalf("%s: GetProduct(%q, %q) não retornou em 10s — travou?", who, tenant, id)
	}
	if g.err != nil {
		t.Fatalf("%s: GetProduct(%q, %q) falhou: %v", who, tenant, id, g.err)
	}
	if g.p.Tenant != tenant || g.p.ID != id {
		t.Fatalf("%s: GetProduct(%q, %q) devolveu o produto de outra loja/ID: %+v", who, tenant, id, g.p)
	}
	return g.p
}

func mustSave(t *testing.T, svc *catalog.Service, p productdb.Product) {
	t.Helper()
	err, ok := within(10*time.Second, func() error {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		return svc.SaveProduct(ctx, p)
	})
	if !ok {
		t.Fatalf("SaveProduct(%s/%s) não retornou em 10s — travou?", p.Tenant, p.ID)
	}
	if err != nil {
		t.Fatalf("SaveProduct(%s/%s): %v", p.Tenant, p.ID, err)
	}
}

// within roda fn numa goroutine e espera até d; ok=false se não terminou.
func within[T any](d time.Duration, fn func() T) (T, bool) {
	ch := make(chan T, 1)
	go func() { ch <- fn() }()
	timer := time.NewTimer(d)
	defer timer.Stop()
	select {
	case v := <-ch:
		return v, true
	case <-timer.C:
		var zero T
		return zero, false
	}
}

func waitGate(t *testing.T, g *productdb.Gate, what string) {
	t.Helper()
	select {
	case <-g.Arrived():
	case <-time.After(5 * time.Second):
		t.Fatalf("%s (esperei 5s)", what)
	}
}

func isNotFound(err error) bool { return errors.Is(err, productdb.ErrNotFound) }

// keyTTL é uma chave do Redis e o seu PTTL (ms; -1 = sem expiração), com o
// instante (desde start) em que ela expira.
type keyTTL struct {
	key      string
	pttl     int64
	expireAt time.Duration
}

// redisKeys lista todas as chaves do banco lógico 0 com seus TTLs.
func (e *env) redisKeys(t *testing.T, start time.Time) []keyTTL {
	t.Helper()
	ctx := testCtx(t, 20*time.Second)
	keys, err := e.admin.Keys(ctx, "*")
	if err != nil {
		t.Fatalf("SCAN: %v", err)
	}
	sort.Strings(keys)
	out := make([]keyTTL, 0, len(keys))
	for _, k := range keys {
		ms, err := e.admin.PTTL(ctx, k)
		if err != nil {
			t.Fatalf("PTTL %q: %v", k, err)
		}
		if ms == -2 { // expirou entre o SCAN e o PTTL (ex.: uma trava de curta duração)
			continue
		}
		out = append(out, keyTTL{key: k, pttl: ms, expireAt: time.Since(start) + time.Duration(ms)*time.Millisecond})
	}
	return out
}

func ms(n int64) string { return (time.Duration(n) * time.Millisecond).Round(time.Second).String() }

func quote(s string) string { return fmt.Sprintf("%q", s) }
