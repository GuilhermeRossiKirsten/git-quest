// Comando demo reproduz, contra o Redis do compose (banco lógico 1), seis
// situações do dia a dia do catálogo e mostra o que o serviço faz em cada
// uma. Faz parte do avaliador — não altere. Rode com ./start.sh.
package main

import (
	"context"
	"errors"
	"fmt"
	"net"
	"os"
	"sync"
	"time"

	"gitquest/catalog/internal/catalog"
	"gitquest/catalog/internal/productdb"
	"gitquest/catalog/internal/redisx"
)

const redisDB = 1

var addr = "redis:6379"

func main() {
	if a := os.Getenv("REDIS_ADDR"); a != "" {
		addr = a
	}
	admin := redisx.New(addr, redisDB)
	defer admin.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := admin.Ping(ctx); err != nil {
		fmt.Fprintf(os.Stderr, "Redis indisponível em %s: %v\n", addr, err)
		os.Exit(1)
	}

	scenarios := []struct {
		title string
		run   func(*redisx.Client)
	}{
		{"Troca de preço com o site aberto", priceChange},
		{"Duas lojas, o mesmo ID de produto", tenants},
		{"Vitrine da Black Friday: 100 acessos simultâneos a um produto fora do cache", stampede},
		{"Robôs consultando IDs que não existem", missing},
		{"Redis travado (aceita a conexão e não responde)", outage},
		{"Quando os produtos carregados juntos vão expirar?", expirations},
	}
	for i, s := range scenarios {
		_ = admin.FlushDB(ctx)
		fmt.Printf("\n[%d] %s\n", i+1, s.title)
		s.run(admin)
	}
	_ = admin.FlushDB(ctx)
	fmt.Println("\nO banco lógico 1 do Redis é esvaziado entre os cenários. Rode ./test.sh para a avaliação completa.")
}

func newService(db *productdb.DB) *catalog.Service {
	return catalog.New(redisx.New(addr, redisDB), db)
}

func get(svc *catalog.Service, tenant, id string) (productdb.Product, time.Duration, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	start := time.Now()
	p, err := svc.GetProduct(ctx, tenant, id)
	return p, time.Since(start), err
}

func save(svc *catalog.Service, p productdb.Product) error {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	return svc.SaveProduct(ctx, p)
}

func priceChange(*redisx.Client) {
	db := productdb.New(30 * time.Millisecond)
	db.Seed(productdb.Product{Tenant: "acme", ID: "furadeira", Name: "Furadeira", PriceCents: 29990})
	web, app := newService(db), newService(db)
	get(web, "acme", "furadeira")

	gate := db.HoldNextSave("acme", "furadeira")
	done := make(chan error, 1)
	go func() {
		done <- save(web, productdb.Product{Tenant: "acme", ID: "furadeira", Name: "Furadeira", PriceCents: 24990})
	}()
	<-gate.Arrived()
	p, _, err := get(app, "acme", "furadeira") // alguém abre a página durante a gravação
	fmt.Printf("    durante a gravação, a réplica 2 leu %d (err=%v)\n", p.PriceCents, err)
	gate.Release()
	if err := <-done; err != nil {
		fmt.Printf("    SaveProduct falhou: %v\n", err)
		return
	}
	for _, r := range []struct {
		name string
		svc  *catalog.Service
	}{{"réplica 1", web}, {"réplica 2", app}} {
		p, _, err := get(r.svc, "acme", "furadeira")
		verdict := "ok"
		if p.PriceCents != 24990 {
			verdict = "PREÇO ANTIGO"
		}
		fmt.Printf("    depois da promoção (24990), a %s lê %d (err=%v) — %s\n", r.name, p.PriceCents, err, verdict)
	}
}

func tenants(*redisx.Client) {
	db := productdb.New(5 * time.Millisecond)
	db.Seed(productdb.Product{Tenant: "acme", ID: "42", Name: "Martelo", PriceCents: 4990},
		productdb.Product{Tenant: "globex", ID: "42", Name: "Bigorna", PriceCents: 89900})
	svc := newService(db)
	for _, tenant := range []string{"acme", "globex"} {
		p, _, err := get(svc, tenant, "42")
		fmt.Printf("    loja %-6s produto 42 → %q da loja %q (err=%v)\n", tenant, p.Name, p.Tenant, err)
	}
}

func stampede(*redisx.Client) {
	db := productdb.New(50 * time.Millisecond)
	db.Seed(productdb.Product{Tenant: "acme", ID: "vitrine", Name: "Smart TV", PriceCents: 299900})
	svc := newService(db)
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() { defer wg.Done(); get(svc, "acme", "vitrine") }()
	}
	wg.Wait()
	fmt.Printf("    consultas ao banco: %d (cada uma custa 50 ms do banco)\n", db.GetCalls("acme", "vitrine"))
}

func missing(*redisx.Client) {
	db := productdb.New(5 * time.Millisecond)
	svc := newService(db)
	notFound := 0
	for i := 0; i < 200; i++ {
		if _, _, err := get(svc, "acme", fmt.Sprintf("sumiu-%d", i%20)); errors.Is(err, productdb.ErrNotFound) {
			notFound++
		}
	}
	fmt.Printf("    200 consultas a 20 IDs inexistentes: %d 'não encontrado', %d consultas ao banco\n",
		notFound, db.TotalGetCalls())
}

func expirations(admin *redisx.Client) {
	db := productdb.New(0)
	svc := newService(db)
	for i := 0; i < 200; i++ {
		db.Seed(productdb.Product{Tenant: "acme", ID: fmt.Sprintf("p%d", i), Name: "Produto", PriceCents: 100})
	}
	for i := 0; i < 200; i++ {
		get(svc, "acme", fmt.Sprintf("p%d", i))
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	keys, err := admin.Keys(ctx, "*")
	if err != nil {
		fmt.Printf("    SCAN: %v\n", err)
		return
	}
	perSecond := map[int64]int{}
	noTTL := 0
	for _, k := range keys {
		ms, err := admin.PTTL(ctx, k)
		switch {
		case err != nil:
			continue
		case ms == -1:
			noTTL++
		case ms >= 0:
			perSecond[ms/1000]++
		}
	}
	worst, at := 0, int64(0)
	for s, n := range perSecond {
		if n > worst {
			worst, at = n, s
		}
	}
	fmt.Printf("    %d chaves; %d sem TTL; expiram em %d segundo(s) distinto(s); o pior segundo (daqui a %ds) concentra %d expirações\n",
		len(keys), noTTL, len(perSecond), at, worst)
}

func outage(*redisx.Client) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		fmt.Printf("    %v\n", err)
		return
	}
	defer ln.Close()
	go func() {
		var conns []net.Conn
		defer func() {
			for _, c := range conns {
				c.Close()
			}
		}()
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			conns = append(conns, c)
		}
	}()
	db := productdb.New(5 * time.Millisecond)
	db.Seed(productdb.Product{Tenant: "acme", ID: "1", Name: "Martelo", PriceCents: 4990})
	svc := catalog.New(redisx.New(ln.Addr().String(), 0), db)
	p, took, err := get(svc, "acme", "1")
	fmt.Printf("    GetProduct com prazo de 3s: %q em %v (err=%v)\n", p.Name, took.Round(time.Millisecond), err)
}
