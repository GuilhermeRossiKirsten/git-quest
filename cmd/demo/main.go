// Demonstração dos incidentes do "Fórum da Vila" na rede simulada.
// Cada caso mostra o que a moderação esperava e o que as réplicas mostram.
// Não altere: é parte do ambiente do desafio.
package main

import (
	"fmt"
	"strings"

	"desafio35/sim"
	"desafio35/src/kv"
)

type cluster struct {
	net *sim.Network
	r   []*kv.Replica
}

func newCluster(cfg sim.Config) cluster {
	c := cluster{net: sim.New(cfg)}
	for i := 0; i < cfg.Nodes; i++ {
		rep := kv.NewReplica(c.net.Endpoint(i))
		c.net.Attach(i, rep)
		c.r = append(c.r, rep)
	}
	return c
}

func (c cluster) drain() {
	if err := c.net.Drain(100000); err != nil {
		fmt.Println("   ", err)
	}
}

func (c cluster) show(key string) string {
	var parts []string
	for i, rep := range c.r {
		v, ok := rep.Get(key)
		if !ok {
			v = "∅"
		}
		parts = append(parts, fmt.Sprintf("r%d=%q", i, v))
	}
	return strings.Join(parts, " ")
}

func header(n int, title string) { fmt.Printf("\n[%d] %s\n", n, title) }

func main() {
	fmt.Println("Fórum da Vila — 3 réplicas, replicação assíncrona, rede simulada (sem relógio de parede real)")

	header(1, "réplica com o relógio 1 minuto adiantado")
	c := newCluster(sim.Config{Nodes: 3, Seed: 1, MinDelay: 5, MaxDelay: 50, Skew: []int64{60_000, 0, 0}})
	c.r[0].Put("pedido:42", "aberto")
	c.drain()
	v, _ := c.r[1].Get("pedido:42")
	fmt.Printf("    r1 lê pedido:42=%q e então grava \"cancelado\"\n", v)
	c.r[1].Put("pedido:42", "cancelado")
	c.drain()
	fmt.Printf("    esperado: todas com \"cancelado\"\n    obtido:   %s\n", c.show("pedido:42"))

	header(2, "resposta chega antes da pergunta (enlace r0→r2 lento)")
	c = newCluster(sim.Config{Nodes: 3, Seed: 2, MinDelay: 10, MaxDelay: 10})
	c.net.Block(0, 2)
	c.r[0].Put("post:1", "Alguém viu meu gato?")
	c.drain()
	c.r[1].Put("reply:1", "Está no telhado!")
	c.drain()
	_, post := c.r[2].Get("post:1")
	reply, _ := c.r[2].Get("reply:1")
	fmt.Printf("    esperado: r2 só mostra a resposta junto com a pergunta\n    obtido:   r2 mostra post:1? %v · reply:1=%q\n", post, reply)
	c.net.Unblock(0, 2)
	c.drain()

	header(3, "rede que entrega cada mensagem duas vezes")
	c = newCluster(sim.Config{Nodes: 3, Seed: 3, MinDelay: 1, MaxDelay: 30, DupProb: 1})
	c.r[0].Add("curtidas", 1)
	c.r[1].Add("curtidas", 1)
	c.drain()
	fmt.Printf("    esperado: curtidas=2 em todas\n    obtido:   r0=%d r1=%d r2=%d\n",
		c.r[0].Counter("curtidas"), c.r[1].Counter("curtidas"), c.r[2].Counter("curtidas"))

	header(4, "duas réplicas editam o mesmo tema ao mesmo tempo")
	c = newCluster(sim.Config{Nodes: 3, Seed: 4, MinDelay: 1, MaxDelay: 100})
	c.r[0].Put("tema", "claro (r0)")
	c.r[2].Put("tema", "escuro (r2)")
	c.drain()
	fmt.Printf("    esperado: o mesmo valor em todas as réplicas\n    obtido:   %s\n", c.show("tema"))
	fmt.Println()
}
