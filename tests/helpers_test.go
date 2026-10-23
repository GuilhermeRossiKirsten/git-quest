package tests

import (
	"fmt"
	"os"
	"strconv"
	"strings"
	"testing"

	"desafio35/sim"
	"desafio35/src/kv"
)

// cluster é um grupo de réplicas ligado a uma rede simulada, com um diário
// (trace) de tudo o que aconteceu para reproduzir falhas.
type cluster struct {
	net   *sim.Network
	r     []*kv.Replica
	trace []string
}

func newCluster(cfg sim.Config) *cluster {
	c := &cluster{net: sim.New(cfg)}
	for i := 0; i < cfg.Nodes; i++ {
		rep := kv.NewReplica(c.net.Endpoint(i))
		c.net.Attach(i, rep)
		c.r = append(c.r, rep)
	}
	c.net.OnDeliver(func(e sim.Event) { c.trace = append(c.trace, e.String()) })
	return c
}

func (c *cluster) logf(format string, args ...any) {
	c.trace = append(c.trace, fmt.Sprintf("t=%dms ", c.net.Now())+fmt.Sprintf(format, args...))
}

func (c *cluster) put(i int, key, value string) {
	c.logf("r%d.Put(%q, %q)", i, key, value)
	c.r[i].Put(key, value)
}

func (c *cluster) add(i int, key string, delta int64) {
	c.logf("r%d.Add(%q, %d)", i, key, delta)
	c.r[i].Add(key, delta)
}

func (c *cluster) get(i int, key string) (string, bool) {
	v, ok := c.r[i].Get(key)
	if ok {
		c.logf("r%d.Get(%q) = %q", i, key, v)
	} else {
		c.logf("r%d.Get(%q) = (ausente)", i, key)
	}
	return v, ok
}

// drain entrega tudo o que está em trânsito (quiescência).
func (c *cluster) drain(t *testing.T) {
	t.Helper()
	if err := c.net.Drain(200000); err != nil {
		t.Fatal(err)
	}
}

// tail devolve as últimas n linhas do diário, para a mensagem de falha.
func (c *cluster) tail(n int) string {
	lines := c.trace
	if len(lines) > n {
		lines = lines[len(lines)-n:]
	}
	return "        últimos eventos:\n          " + strings.Join(lines, "\n          ")
}

// state descreve o que cada réplica mostra para as chaves e contadores dados.
func (c *cluster) state(keys, counters []string) []string {
	out := make([]string, len(c.r))
	for i, rep := range c.r {
		var b strings.Builder
		for _, k := range keys {
			v, ok := rep.Get(k)
			if !ok {
				v = "∅"
			}
			fmt.Fprintf(&b, "%s=%s ", k, v)
		}
		for _, k := range counters {
			fmt.Fprintf(&b, "#%s=%d ", k, rep.Counter(k))
		}
		out[i] = strings.TrimSpace(b.String())
	}
	return out
}

// seeds devolve as sementes de um teste de propriedade: 1..n, ou só a de
// GQ_SEED (para reproduzir uma falha com o diário completo).
func seeds(t *testing.T, n int) (list []uint64, verbose bool) {
	if s := os.Getenv("GQ_SEED"); s != "" {
		v, err := strconv.ParseUint(s, 10, 64)
		if err != nil {
			t.Fatalf("GQ_SEED=%q não é um número", s)
		}
		return []uint64{v}, true
	}
	for i := 1; i <= n; i++ {
		list = append(list, uint64(i))
	}
	return list, false
}

// forEachSeed roda check para cada semente e para na primeira que falhar,
// dizendo como reproduzi-la.
func forEachSeed(t *testing.T, n int, check func(seed uint64) (c *cluster, problem string)) {
	t.Helper()
	list, verbose := seeds(t, n)
	for _, seed := range list {
		c, problem := check(seed)
		if verbose && c != nil {
			t.Logf("diário completo da semente %d:\n  %s", seed, strings.Join(c.trace, "\n  "))
		}
		if problem != "" {
			msg := fmt.Sprintf("semente %d: %s\n        reproduza com: GQ_SEED=%d ./test.sh", seed, problem, seed)
			if c != nil {
				msg += "\n" + c.tail(25)
			}
			t.Fatal(msg)
		}
	}
	if !verbose {
		t.Logf("%d sementes verificadas", len(list))
	}
}
