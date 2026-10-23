package tests

import (
	"fmt"
	"math/rand/v2"
	"testing"

	"desafio35/src/clock"
)

// TestLamport: Tick conta eventos locais; Observe é o recebimento:
// max(local, remoto) + 1 (Tanenbaum & van Steen, seção 6.2.1).
func TestLamport(t *testing.T) {
	var c clock.Lamport
	if c.Now() != 0 {
		t.Fatalf("relógio novo deveria valer 0, vale %d", c.Now())
	}
	if got := c.Tick(); got != 1 {
		t.Errorf("primeiro Tick: esperado 1, obtido %d", got)
	}
	c.Tick() // 2
	if got := c.Observe(10); got != 11 {
		t.Errorf("relógio em 2 recebe carimbo 10: esperado 11 (max+1), obtido %d", got)
	}
	if got := c.Observe(3); got != 12 {
		t.Errorf("relógio em 11 recebe carimbo 3: esperado 12, obtido %d", got)
	}
	if got := c.Observe(12); got != 13 {
		t.Errorf("relógio em 12 recebe carimbo 12: esperado 13, obtido %d", got)
	}
	if got := c.Now(); got != 13 {
		t.Errorf("Now depois dos eventos: esperado 13, obtido %d", got)
	}

	// Três processos que andam em ritmos diferentes (como na figura dos
	// relógios de 6, 8 e 10 tiques): toda mensagem precisa chegar com
	// carimbo de recebimento maior que o de envio.
	rng := rand.New(rand.NewPCG(35, 6))
	procs := make([]clock.Lamport, 3)
	for step := 0; step < 2000; step++ {
		from := rng.IntN(3)
		for range 1 + from*3 { // o processo 2 anda bem mais rápido
			procs[from].Tick()
		}
		sent := procs[from].Tick()
		to := (from + 1 + rng.IntN(2)) % 3
		recv := procs[to].Observe(sent)
		if recv <= sent {
			t.Fatalf("passo %d: P%d enviou com carimbo %d e P%d recebeu com %d — o recebimento precisa ser posterior ao envio",
				step, from, sent, to, recv)
		}
	}
}

// historyEvent é um evento numa história sintética de processos.
type historyEvent struct {
	proc int
	vc   clock.Vector
	// preds: índices dos eventos imediatamente anteriores (no processo e,
	// para recebimentos, o envio correspondente).
	preds []int
}

// TestVetor: Compare precisa reproduzir exatamente a relação "aconteceu
// antes" — inclusive dizer Concurrent quando nenhum precede o outro.
func TestVetor(t *testing.T) {
	v := func(xs ...uint64) clock.Vector { return clock.Vector(xs) }
	cases := []struct {
		a, b clock.Vector
		want clock.Order
	}{
		{v(0, 0, 0), v(0, 0, 0), clock.Equal},
		{v(1, 2, 3), v(1, 2, 3), clock.Equal},
		{v(1, 0, 0), v(2, 0, 0), clock.Before},
		{v(1, 0, 0), v(1, 1, 0), clock.Before},
		{v(1, 2, 0), v(1, 2, 5), clock.Before},
		{v(2, 1, 0), v(1, 1, 0), clock.After},
		{v(1, 0, 0), v(0, 1, 0), clock.Concurrent},
		{v(0, 1, 0), v(1, 0, 0), clock.Concurrent},
		{v(1, 3, 0), v(2, 1, 0), clock.Concurrent},
		{v(2, 1, 0), v(1, 3, 0), clock.Concurrent},
		{v(0, 0, 4), v(3, 1, 0), clock.Concurrent},
	}
	for _, tc := range cases {
		if got := tc.a.Compare(tc.b); got != tc.want {
			t.Errorf("%v.Compare(%v): esperado %v, obtido %v", tc.a, tc.b, tc.want, got)
		}
	}

	// Merge e Clone.
	a := v(1, 5, 2)
	b := a.Clone()
	b.Increment(0)
	if a[0] != 1 {
		t.Errorf("Clone devolveu um vetor que compartilha memória com o original")
	}
	a.Merge(v(3, 1, 2))
	if fmt.Sprint(a) != fmt.Sprint(v(3, 5, 2)) {
		t.Errorf("[1 5 2] Merge [3 1 2]: esperado [3 5 2], obtido %v", a)
	}

	// Histórias aleatórias: compara Compare com a verdade calculada no grafo
	// de eventos (alcançabilidade).
	for seed := uint64(1); seed <= 200; seed++ {
		if msg := checkHistory(seed); msg != "" {
			t.Fatalf("história aleatória (semente %d): %s", seed, msg)
		}
	}
}

func checkHistory(seed uint64) string {
	rng := rand.New(rand.NewPCG(seed, 99))
	n := 3 + int(seed%3)
	cur := make([]clock.Vector, n)
	last := make([]int, n)
	for i := range cur {
		cur[i] = clock.NewVector(n)
		last[i] = -1
	}
	var evs []historyEvent
	type inflight struct {
		vc  clock.Vector
		idx int
	}
	var msgs []inflight
	for step := 0; step < 40; step++ {
		p := rng.IntN(n)
		e := historyEvent{proc: p}
		if last[p] >= 0 {
			e.preds = append(e.preds, last[p])
		}
		switch k := rng.IntN(3); {
		case k == 0 && len(msgs) > 0: // recebimento
			j := rng.IntN(len(msgs))
			m := msgs[j]
			msgs = append(msgs[:j], msgs[j+1:]...)
			cur[p].Merge(m.vc)
			cur[p].Increment(p)
			e.preds = append(e.preds, m.idx)
		case k == 1: // envio
			cur[p].Increment(p)
			msgs = append(msgs, inflight{vc: cur[p].Clone(), idx: len(evs)})
		default: // evento local
			cur[p].Increment(p)
		}
		e.vc = cur[p].Clone()
		last[p] = len(evs)
		evs = append(evs, e)
	}
	// reach[i][j]: o evento i aconteceu antes do evento j.
	reach := make([][]bool, len(evs))
	for j := range evs {
		reach[j] = make([]bool, len(evs))
	}
	for j, e := range evs {
		for _, p := range e.preds {
			reach[p][j] = true
			for i := range evs {
				if reach[i][p] {
					reach[i][j] = true
				}
			}
		}
	}
	for i := range evs {
		for j := range evs {
			want := clock.Concurrent
			switch {
			case i == j:
				want = clock.Equal
			case reach[i][j]:
				want = clock.Before
			case reach[j][i]:
				want = clock.After
			}
			if got := evs[i].vc.Compare(evs[j].vc); got != want {
				return fmt.Sprintf("evento %d de P%d %v e evento %d de P%d %v: esperado %v, Compare disse %v",
					i, evs[i].proc, evs[i].vc, j, evs[j].proc, evs[j].vc, want, got)
			}
		}
	}
	return ""
}
