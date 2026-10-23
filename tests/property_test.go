package tests

import (
	"fmt"
	"math/rand/v2"
	"slices"
	"strings"
	"testing"

	"desafio35/sim"
)

const propertySeeds = 500

func rngFor(seed, salt uint64) *rand.Rand { return rand.New(rand.NewPCG(seed, salt)) }

// TestPropriedade_Convergencia: escritas em várias réplicas, ao mesmo tempo,
// com relógios desalinhados e rede que reordena. Depois da quiescência todas
// as réplicas precisam mostrar exatamente o mesmo estado — e o mesmo estado
// de novo se a semente for repetida (determinismo).
func TestPropriedade_Convergencia(t *testing.T) {
	keys := []string{"a", "b", "c", "d"}
	counters := []string{"x", "y"}
	run := func(seed uint64) (*cluster, int64, int64) {
		rng := rngFor(seed, 1)
		n := 3 + int(seed%3)
		skew := make([]int64, n)
		for i := range skew {
			skew[i] = 10 * (rng.Int64N(11) - 5) // -50..50 ms
		}
		c := newCluster(sim.Config{Nodes: n, Seed: seed, MinDelay: 1, MaxDelay: 120, Skew: skew})
		var sumX, sumY int64
		for op := range 60 {
			r := rng.IntN(n)
			if rng.IntN(10) < 7 {
				c.put(r, keys[rng.IntN(len(keys))], fmt.Sprintf("v%d@r%d", op, r))
			} else {
				d := 1 + rng.Int64N(5)
				k := counters[rng.IntN(2)]
				if k == "x" {
					sumX += d
				} else {
					sumY += d
				}
				c.add(r, k, d)
			}
			c.net.Advance(10 * rng.Int64N(4))
		}
		return c, sumX, sumY
	}
	forEachSeed(t, propertySeeds, func(seed uint64) (*cluster, string) {
		c, sumX, sumY := run(seed)
		if err := c.net.Drain(200000); err != nil {
			return c, err.Error()
		}
		st := c.state(keys, counters)
		if !allEqual(st) {
			return c, "as réplicas não convergiram:\n          " + numbered(st)
		}
		for i, rep := range c.r {
			if rep.Counter("x") != sumX || rep.Counter("y") != sumY {
				return c, fmt.Sprintf("r%d mostra #x=%d #y=%d; a soma das operações é #x=%d #y=%d",
					i, rep.Counter("x"), rep.Counter("y"), sumX, sumY)
			}
		}
		if seed <= 25 {
			again, _, _ := run(seed)
			if err := again.net.Drain(200000); err != nil {
				return again, err.Error()
			}
			if st2 := again.state(keys, counters); !slices.Equal(st, st2) {
				return c, fmt.Sprintf("a mesma semente rodada duas vezes deu estados diferentes:\n          1ª: %s\n          2ª: %s",
					st[0], st2[0])
			}
		}
		return c, ""
	})
}

// TestPropriedade_Causalidade: um fórum. Respostas só são escritas por quem
// já vê a pergunta; edições só por quem vê a versão que está editando.
// Depois de CADA entrega, nenhuma réplica pode mostrar uma resposta sem a
// pergunta. No fim, todas convergem, nada fica retido e nenhuma edição
// é desfeita por uma versão que ela própria sucedeu.
func TestPropriedade_Causalidade(t *testing.T) {
	forEachSeed(t, propertySeeds, func(seed uint64) (*cluster, string) {
		rng := rngFor(seed, 2)
		n := 3 + int(seed%3)
		skew := make([]int64, n)
		for i := range skew {
			skew[i] = rng.Int64N(6001) - 3000 // ±3 s
		}
		c := newCluster(sim.Config{Nodes: n, Seed: seed, MinDelay: 1, MaxDelay: 200, DupProb: 0.2, Skew: skew})

		posts := 0
		replied := map[int]bool{}
		dominated := map[string]bool{} // versões que alguma edição sucedeu
		problem := ""
		checkReplica := func(i int) {
			if problem != "" {
				return
			}
			for k := 0; k < posts; k++ {
				if _, ok := c.r[i].Get(fmt.Sprintf("reply:%d", k)); !ok {
					continue
				}
				if _, ok := c.r[i].Get(fmt.Sprintf("post:%d", k)); !ok {
					problem = fmt.Sprintf("r%d mostra reply:%d sem mostrar post:%d — efeito visível sem a causa", i, k, k)
					return
				}
			}
		}
		c.net.OnDeliver(func(e sim.Event) {
			c.trace = append(c.trace, e.String())
			checkReplica(e.To)
		})

		edits := 0
		for step := 0; step < 60 && problem == ""; step++ {
			r := rng.IntN(n)
			switch a := rng.IntN(20); {
			case a < 6 || posts == 0:
				c.put(r, fmt.Sprintf("post:%d", posts), fmt.Sprintf("p%d@r%d", posts, r))
				posts++
			case a < 13:
				k := rng.IntN(posts)
				if _, ok := c.get(r, fmt.Sprintf("post:%d", k)); ok && !replied[k] {
					replied[k] = true
					c.put(r, fmt.Sprintf("reply:%d", k), fmt.Sprintf("re:p%d@r%d", k, r))
				}
			default:
				k := rng.IntN(posts)
				if v, ok := c.get(r, fmt.Sprintf("post:%d", k)); ok {
					edits++
					dominated[v] = true
					c.put(r, fmt.Sprintf("post:%d", k), fmt.Sprintf("%s|e%d@r%d", v, edits, r))
				}
			}
			checkReplica(r)
			c.net.Advance(rng.Int64N(60))
		}
		if problem != "" {
			return c, problem
		}
		if err := c.net.Drain(200000); err != nil {
			return c, err.Error()
		}
		if problem != "" {
			return c, problem
		}
		var keys []string
		for k := 0; k < posts; k++ {
			keys = append(keys, fmt.Sprintf("post:%d", k))
			if replied[k] {
				keys = append(keys, fmt.Sprintf("reply:%d", k))
			}
		}
		st := c.state(keys, nil)
		if !allEqual(st) {
			return c, "depois da quiescência as réplicas não convergiram:\n          " + numbered(st)
		}
		for _, k := range keys {
			v, ok := c.r[0].Get(k)
			if !ok {
				return c, fmt.Sprintf("depois da quiescência %s não aparece em nenhuma réplica — mensagem retida para sempre?", k)
			}
			if dominated[v] {
				return c, fmt.Sprintf("%s terminou com %q, mas alguém leu essa versão e escreveu uma edição por cima — a escrita causalmente posterior se perdeu", k, v)
			}
		}
		return c, ""
	})
}

// TestPropriedade_Idempotencia: a rede duplica metade das mensagens. Nenhuma
// réplica pode contar uma operação duas vezes — nem por um instante.
func TestPropriedade_Idempotencia(t *testing.T) {
	counters := []string{"curtidas", "visualizacoes"}
	forEachSeed(t, propertySeeds, func(seed uint64) (*cluster, string) {
		rng := rngFor(seed, 3)
		n := 3 + int(seed%3)
		c := newCluster(sim.Config{Nodes: n, Seed: seed, MinDelay: 1, MaxDelay: 100, DupProb: 0.5})
		issued := map[string]int64{}
		problem := ""
		c.net.OnDeliver(func(e sim.Event) {
			c.trace = append(c.trace, e.String())
			for _, k := range counters {
				if got := c.r[e.To].Counter(k); problem == "" && got > issued[k] {
					problem = fmt.Sprintf("r%d mostra #%s=%d, mas só %d foi somado até agora — alguma operação foi aplicada duas vezes",
						e.To, k, got, issued[k])
				}
			}
		})
		for op := range 50 {
			r := rng.IntN(n)
			if rng.IntN(5) == 0 {
				c.put(r, "titulo", fmt.Sprintf("t%d@r%d", op, r))
			} else {
				k := counters[rng.IntN(2)]
				d := 1 + rng.Int64N(3)
				issued[k] += d
				c.add(r, k, d)
			}
			c.net.Advance(rng.Int64N(30))
		}
		if err := c.net.Drain(200000); err != nil {
			return c, err.Error()
		}
		if problem != "" {
			return c, problem
		}
		for i, rep := range c.r {
			for _, k := range counters {
				if got := rep.Counter(k); got != issued[k] {
					return c, fmt.Sprintf("depois da quiescência r%d mostra #%s=%d; esperado %d", i, k, got, issued[k])
				}
			}
		}
		if st := c.state([]string{"titulo"}, counters); !allEqual(st) {
			return c, "as réplicas não convergiram:\n          " + numbered(st)
		}
		return c, ""
	})
}

func numbered(st []string) string {
	lines := make([]string, len(st))
	for i, s := range st {
		lines[i] = fmt.Sprintf("r%d: %s", i, s)
	}
	return strings.Join(lines, "\n          ")
}
