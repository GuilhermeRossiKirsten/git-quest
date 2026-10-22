// Package productdb simula o banco de produtos: a fonte da verdade, lenta e
// cara, que o cache existe para proteger. Guarda os produtos em memória,
// aplica uma latência artificial a cada operação e conta as chamadas.
//
// Faz parte do avaliador — não altere. Os testes usam os contadores
// (GetCalls) e as "travas" (HoldNextGet/HoldNextSave) para reproduzir de
// forma determinística as intercalações que em produção dependem de sorte.
package productdb

import (
	"context"
	"errors"
	"sync"
	"time"
)

// ErrNotFound indica que o produto não existe (no banco — e, portanto, para o cliente).
var ErrNotFound = errors.New("produto não encontrado")

// Product é uma linha da tabela de produtos. Cada loja (Tenant) tem o seu
// próprio catálogo: o mesmo ID pode existir em lojas diferentes.
type Product struct {
	Tenant     string `json:"tenant"`
	ID         string `json:"id"`
	Name       string `json:"name"`
	PriceCents int64  `json:"price_cents"`
	// Version começa em 1 e o banco a incrementa a cada Save.
	Version int64 `json:"version"`
}

type key struct{ tenant, id string }

// DB é seguro para uso concorrente.
type DB struct {
	latency time.Duration

	mu        sync.Mutex
	rows      map[key]Product
	gets      map[key]int
	totalGets int
	getHolds  map[key]*Gate
	saveHolds map[key]*Gate
}

// New cria um banco vazio; cada Get e cada Save leva pelo menos latency.
func New(latency time.Duration) *DB {
	return &DB{
		latency:   latency,
		rows:      map[key]Product{},
		gets:      map[key]int{},
		getHolds:  map[key]*Gate{},
		saveHolds: map[key]*Gate{},
	}
}

// Seed insere produtos diretamente (sem latência nem contadores), com Version 1.
func (db *DB) Seed(ps ...Product) {
	db.mu.Lock()
	defer db.mu.Unlock()
	for _, p := range ps {
		p.Version = 1
		db.rows[key{p.Tenant, p.ID}] = p
	}
}

// Get lê o produto. Devolve ErrNotFound se ele não existe.
func (db *DB) Get(ctx context.Context, tenant, id string) (Product, error) {
	if err := sleep(ctx, db.latency); err != nil {
		return Product{}, err
	}
	k := key{tenant, id}
	db.mu.Lock()
	p, ok := db.rows[k] // o "snapshot" que esta leitura devolve
	db.gets[k]++
	db.totalGets++
	g := db.getHolds[k]
	delete(db.getHolds, k)
	db.mu.Unlock()

	if g != nil {
		if err := g.hold(ctx); err != nil {
			return Product{}, err
		}
	}
	if !ok {
		return Product{}, ErrNotFound
	}
	return p, nil
}

// Save grava (insere ou atualiza) o produto e devolve a linha gravada, com a
// nova Version.
func (db *DB) Save(ctx context.Context, p Product) (Product, error) {
	k := key{p.Tenant, p.ID}
	db.mu.Lock()
	g := db.saveHolds[k]
	delete(db.saveHolds, k)
	db.mu.Unlock()

	if g != nil {
		if err := g.hold(ctx); err != nil {
			return Product{}, err
		}
	}
	if err := sleep(ctx, db.latency); err != nil {
		return Product{}, err
	}
	db.mu.Lock()
	defer db.mu.Unlock()
	p.Version = db.rows[k].Version + 1
	db.rows[k] = p
	return p, nil
}

// Peek lê o produto sem latência e sem contar a chamada (para os testes conferirem o banco).
func (db *DB) Peek(tenant, id string) (Product, bool) {
	db.mu.Lock()
	defer db.mu.Unlock()
	p, ok := db.rows[key{tenant, id}]
	return p, ok
}

// GetCalls devolve quantas vezes Get foi chamado para o produto.
func (db *DB) GetCalls(tenant, id string) int {
	db.mu.Lock()
	defer db.mu.Unlock()
	return db.gets[key{tenant, id}]
}

// TotalGetCalls devolve quantas vezes Get foi chamado, para qualquer produto.
func (db *DB) TotalGetCalls() int {
	db.mu.Lock()
	defer db.mu.Unlock()
	return db.totalGets
}

// HoldNextGet trava a PRÓXIMA leitura do produto logo depois de ela ler o
// banco: ela já tem o valor nas mãos, mas só o devolve depois de Release —
// como uma consulta lenta. As leituras seguintes não são travadas.
func (db *DB) HoldNextGet(tenant, id string) *Gate {
	g := newGate()
	db.mu.Lock()
	db.getHolds[key{tenant, id}] = g
	db.mu.Unlock()
	return g
}

// HoldNextSave trava a PRÓXIMA gravação do produto ANTES de ela alterar o
// banco, até Release — como uma transação demorada.
func (db *DB) HoldNextSave(tenant, id string) *Gate {
	g := newGate()
	db.mu.Lock()
	db.saveHolds[key{tenant, id}] = g
	db.mu.Unlock()
	return g
}

// Gate é uma trava de uso único criada por HoldNextGet/HoldNextSave.
type Gate struct {
	arrived     chan struct{}
	release     chan struct{}
	arriveOnce  sync.Once
	releaseOnce sync.Once
}

func newGate() *Gate {
	return &Gate{arrived: make(chan struct{}), release: make(chan struct{})}
}

// Arrived é fechado quando a operação travada chega à trava.
func (g *Gate) Arrived() <-chan struct{} { return g.arrived }

// Release libera a operação travada (pode ser chamado mais de uma vez).
func (g *Gate) Release() { g.releaseOnce.Do(func() { close(g.release) }) }

func (g *Gate) hold(ctx context.Context) error {
	g.arriveOnce.Do(func() { close(g.arrived) })
	select {
	case <-g.release:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}

func sleep(ctx context.Context, d time.Duration) error {
	if d <= 0 {
		return ctx.Err()
	}
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-t.C:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}
