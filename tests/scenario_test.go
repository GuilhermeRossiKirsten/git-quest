package tests

import (
	"fmt"
	"strings"
	"sync"
	"testing"

	"desafio35/sim"
)

// TestReplica_CaminhoFeliz: um único escritor, relógios certos, rede calma.
// (Já funciona no código inicial: é a prova de que o avaliador está de pé.)
func TestReplica_CaminhoFeliz(t *testing.T) {
	c := newCluster(sim.Config{Nodes: 3, Seed: 1, MinDelay: 10, MaxDelay: 10})
	for i := 1; i <= 5; i++ {
		c.put(0, "aviso", fmt.Sprintf("versão %d", i))
		c.add(0, "leituras", 1)
		c.net.Advance(50)
	}
	c.drain(t)
	for i := range c.r {
		if v, _ := c.get(i, "aviso"); v != "versão 5" {
			t.Errorf("r%d mostra aviso=%q; esperado \"versão 5\"", i, v)
		}
		if n := c.r[i].Counter("leituras"); n != 5 {
			t.Errorf("r%d mostra leituras=%d; esperado 5", i, n)
		}
	}
}

// TestCenario_RelogioAdiantado: a réplica r0 tem o relógio físico adiantado.
// r1 lê o pedido "aberto" e o cancela — a escrita de r1 é causalmente
// posterior e precisa vencer em todas as réplicas, adiantadas ou não.
func TestCenario_RelogioAdiantado(t *testing.T) {
	for _, skew := range []int64{60_000, 1_500, 30} {
		for seed := uint64(1); seed <= 10; seed++ {
			c := newCluster(sim.Config{Nodes: 3, Seed: seed, MinDelay: 5, MaxDelay: 80,
				Skew: []int64{skew, 0, -skew / 3}})
			c.put(0, "pedido:42", "aberto")
			c.drain(t)
			c.net.Advance(20)
			if v, _ := c.get(1, "pedido:42"); v != "aberto" {
				t.Fatalf("relógio de r0 adiantado %dms, semente %d: depois da quiescência r1 deveria ver \"aberto\", vê %q\n%s",
					skew, seed, v, c.tail(10))
			}
			c.put(1, "pedido:42", "cancelado")
			c.drain(t)
			for i := range c.r {
				if v, _ := c.get(i, "pedido:42"); v != "cancelado" {
					t.Fatalf("relógio de r0 adiantado %dms, semente %d: r1 cancelou DEPOIS de ler \"aberto\", mas r%d terminou com %q (esperado \"cancelado\")\n%s",
						skew, seed, i, v, c.tail(12))
				}
			}
		}
	}
}

// TestCenario_PostAntesDaResposta: o enlace r0→r2 está lento. r1 vê a
// pergunta de r0 e responde; r2 recebe a resposta primeiro. Nenhum leitor de
// r2 pode ver a resposta sem a pergunta.
func TestCenario_PostAntesDaResposta(t *testing.T) {
	c := newCluster(sim.Config{Nodes: 3, Seed: 7, MinDelay: 10, MaxDelay: 10})
	c.net.Block(0, 2)
	c.put(0, "post:1", "Alguém viu meu gato?")
	c.drain(t)
	if _, ok := c.get(1, "post:1"); !ok {
		t.Fatalf("r1 deveria ver post:1 depois da quiescência\n%s", c.tail(10))
	}
	c.put(1, "reply:1", "Está no telhado!")
	c.drain(t)
	_, post := c.get(2, "post:1")
	_, reply := c.get(2, "reply:1")
	if reply && !post {
		t.Fatalf("r2 mostra reply:1 mas não post:1 — o leitor viu o efeito sem a causa\n%s", c.tail(10))
	}
	c.net.Unblock(0, 2)
	c.drain(t)
	for _, k := range []string{"post:1", "reply:1"} {
		if _, ok := c.get(2, k); !ok {
			t.Fatalf("depois que a pergunta chegou, r2 deveria mostrar %s (a resposta retida precisa ser liberada)\n%s", k, c.tail(10))
		}
	}
}

// TestCenario_EscritasConcorrentes: escritas concorrentes na mesma chave são
// resolvidas pela ordem total (Lamport, ID da réplica) — o mesmo vencedor em
// todas as réplicas, qualquer que seja a ordem de chegada ou o relógio físico.
func TestCenario_EscritasConcorrentes(t *testing.T) {
	for seed := uint64(1); seed <= 20; seed++ {
		// Empate no Lamport (as duas são a 1ª escrita da réplica): vence a de
		// maior ID, mesmo que a outra tenha sido feita depois e com relógio
		// físico mais à frente.
		c := newCluster(sim.Config{Nodes: 3, Seed: seed, MinDelay: 1, MaxDelay: 100,
			Skew: []int64{1000, 0, 0}})
		c.put(2, "tema", "escuro (r2)")
		c.net.Advance(1)
		c.put(0, "tema", "claro (r0)")
		c.drain(t)
		if st := c.state([]string{"tema"}, nil); !allEqual(st) || st[0] != "tema=escuro (r2)" {
			t.Fatalf("semente %d: r2 e r0 escreveram \"tema\" concorrentemente, ambas com Lamport 1; esperado \"escuro (r2)\" (maior ID) em todas, obtido %s\n%s",
				seed, strings.Join(st, " | "), c.tail(12))
		}

		// Lamports diferentes: r0 já fez 5 escritas, então "A" tem carimbo 6;
		// "B" (1ª escrita de r1, carimbo 1) foi feita depois, com relógio
		// físico adiantado — e mesmo assim perde.
		c = newCluster(sim.Config{Nodes: 3, Seed: seed, MinDelay: 1, MaxDelay: 100,
			Skew: []int64{0, 5000, 0}})
		for i := range 5 {
			c.put(0, fmt.Sprintf("rascunho:%d", i), "…")
		}
		c.put(0, "titulo", "A (r0)")
		c.net.Advance(3)
		c.put(1, "titulo", "B (r1)")
		c.drain(t)
		if st := c.state([]string{"titulo"}, nil); !allEqual(st) || st[0] != "titulo=A (r0)" {
			t.Fatalf("semente %d: \"A\" (Lamport 6, r0) e \"B\" (Lamport 1, r1) são concorrentes; esperado \"A (r0)\" em todas, obtido %s\n%s",
				seed, strings.Join(st, " | "), c.tail(12))
		}
	}
}

// TestCenario_MensagemDuplicada: a rede entrega cada mensagem duas vezes.
func TestCenario_MensagemDuplicada(t *testing.T) {
	c := newCluster(sim.Config{Nodes: 4, Seed: 3, MinDelay: 1, MaxDelay: 30, DupProb: 1})
	c.add(0, "curtidas", 1)
	c.add(1, "curtidas", 2)
	c.add(3, "curtidas", 4)
	c.put(2, "status", "publicado")
	c.drain(t)
	for i := range c.r {
		if n := c.r[i].Counter("curtidas"); n != 7 {
			t.Errorf("r%d mostra curtidas=%d; esperado 7 (1+2+4) — cada mensagem chegou duas vezes", i, n)
		}
		if v, _ := c.get(i, "status"); v != "publicado" {
			t.Errorf("r%d mostra status=%q; esperado \"publicado\"", i, v)
		}
	}
}

// TestReplica_AcessoConcorrente: clientes em várias goroutines enquanto a rede
// entrega mensagens em outra. Roda com -race.
func TestReplica_AcessoConcorrente(t *testing.T) {
	c := newCluster(sim.Config{Nodes: 3, Seed: 11, MinDelay: 1, MaxDelay: 20})
	c.net.OnDeliver(nil) // o diário não é seguro entre goroutines
	const perClient = 200
	var wg sync.WaitGroup
	done := make(chan struct{})
	for i := range c.r {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := range perClient {
				c.r[i].Add("visitas", 1)
				c.r[i].Put(fmt.Sprintf("ultimo:%d", i), fmt.Sprint(j))
				c.r[i].Get("ultimo:0")
				c.r[i].Counter("visitas")
			}
		}()
	}
	go func() { wg.Wait(); close(done) }()
	for running := true; running; {
		select {
		case <-done:
			running = false
		default:
			if !c.net.Step() {
				c.net.Advance(1)
			}
		}
	}
	c.drain(t)
	want := int64(perClient * len(c.r))
	for i := range c.r {
		if n := c.r[i].Counter("visitas"); n != want {
			t.Errorf("r%d mostra visitas=%d; esperado %d", i, n, want)
		}
		for k := range c.r {
			if v, _ := c.r[i].Get(fmt.Sprintf("ultimo:%d", k)); v != fmt.Sprint(perClient-1) {
				t.Errorf("r%d mostra ultimo:%d=%q; esperado %q", i, k, v, fmt.Sprint(perClient-1))
			}
		}
	}
}

func allEqual(xs []string) bool {
	for _, x := range xs {
		if x != xs[0] {
			return false
		}
	}
	return true
}
