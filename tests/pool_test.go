// Testes de comportamento do pacote pool. Rode sempre com o detector de
// corridas:
//
//	go test -race -count=1 -timeout 60s ./tests/
//
// Os testes não são paralelos (nenhum t.Parallel): a checagem de vazamento
// compara a contagem global de goroutines antes e depois de cada chamada.
package pool_test

import (
	"context"
	"errors"
	"fmt"
	"runtime"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"gitquest/pool/src/pool"
)

var errBoom = errors.New("boom")

// probe mede quantas chamadas de fn estão em andamento e o pico observado.
type probe struct {
	running, peak, calls atomic.Int64
}

func (p *probe) enter() {
	p.calls.Add(1)
	cur := p.running.Add(1)
	for {
		old := p.peak.Load()
		if cur <= old || p.peak.CompareAndSwap(old, cur) {
			return
		}
	}
}

func (p *probe) leave() { p.running.Add(-1) }

// sleepCtx dorme d ou até ctx ser cancelado, o que vier primeiro.
func sleepCtx(ctx context.Context, d time.Duration) error {
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-t.C:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}

func seq(n int) []int {
	s := make([]int, n)
	for i := range s {
		s[i] = i
	}
	return s
}

type outcome[R any] struct {
	results []R
	err     error
	elapsed time.Duration
}

// run chama pool.Run numa goroutine separada para que um deadlock no código
// testado reprove apenas este teste, com uma mensagem clara, em vez de
// estourar o timeout da suíte inteira.
func run[T, R any](t *testing.T, ctx context.Context, workers int, in []T, fn func(context.Context, T) (R, error)) outcome[R] {
	t.Helper()
	ch := make(chan outcome[R], 1)
	go func() {
		start := time.Now()
		r, err := pool.Run(ctx, workers, in, fn)
		ch <- outcome[R]{r, err, time.Since(start)}
	}()
	select {
	case o := <-ch:
		return o
	case <-time.After(10 * time.Second):
		t.Fatalf("pool.Run não retornou em 10s (deadlock? alguém esperando um canal que nunca recebe?)")
		panic("inalcançável")
	}
}

// goroutines devolve o stack de cada goroutine viva, indexado pelo id.
func goroutines() map[string]string {
	buf := make([]byte, 1<<20)
	for {
		n := runtime.Stack(buf, true)
		if n < len(buf) {
			buf = buf[:n]
			break
		}
		buf = make([]byte, 2*len(buf))
	}
	m := make(map[string]string)
	for _, g := range strings.Split(string(buf), "\n\n") {
		// Cabeçalho de cada bloco: "goroutine 42 [chan send]:".
		if f := strings.Fields(g); len(f) >= 2 && f[0] == "goroutine" {
			m[f[1]] = g
		}
	}
	return m
}

// expectNoLeak espera até 1s que todas as goroutines criadas desde o
// instantâneo `before` terminem e, se alguma sobrar, mostra onde está parada.
func expectNoLeak(t *testing.T, before map[string]string) {
	t.Helper()
	deadline := time.Now().Add(time.Second)
	var extra []string
	for {
		extra = extra[:0]
		for id, g := range goroutines() {
			if _, ok := before[id]; !ok {
				extra = append(extra, g)
			}
		}
		if len(extra) == 0 || time.Now().After(deadline) {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if len(extra) == 0 {
		return
	}
	t.Errorf("%d goroutine(s) criadas durante pool.Run continuam vivas 1s depois de ele retornar", len(extra))
	for i, g := range extra {
		if i == 1 {
			t.Logf("(mais %d goroutine(s) omitidas)", len(extra)-i)
			break
		}
		t.Logf("goroutine vazada:\n%s", g)
	}
}

func TestEdgeCases(t *testing.T) {
	ctx := context.Background()
	for _, w := range []int{0, -1} {
		t.Run(fmt.Sprintf("workers=%d", w), func(t *testing.T) {
			var p probe
			o := run(t, ctx, w, seq(3), func(context.Context, int) (int, error) {
				p.enter()
				defer p.leave()
				return 0, nil
			})
			if !errors.Is(o.err, pool.ErrInvalidWorkers) {
				t.Errorf("workers=%d: esperado erro com errors.Is(err, pool.ErrInvalidWorkers), veio %v", w, o.err)
			}
			if o.results != nil {
				t.Errorf("workers=%d: esperado results nil junto com o erro, veio %v", w, o.results)
			}
			if c := p.calls.Load(); c != 0 {
				t.Errorf("workers=%d: fn foi chamada %d vez(es); com configuração inválida não deveria ser chamada", w, c)
			}
		})
	}
	for name, in := range map[string][]int{"entradas vazias": {}, "entradas nil": nil} {
		t.Run(name, func(t *testing.T) {
			var p probe
			o := run(t, ctx, 4, in, func(context.Context, int) (int, error) {
				p.enter()
				defer p.leave()
				return 0, nil
			})
			if o.err != nil || len(o.results) != 0 {
				t.Errorf("%s: esperado ([], nil), veio (%v, %v)", name, o.results, o.err)
			}
			if c := p.calls.Load(); c != 0 {
				t.Errorf("%s: fn foi chamada %d vez(es)", name, c)
			}
		})
	}
}

func TestConcurrencyLimit(t *testing.T) {
	cases := []struct {
		workers, items int
		wantPeak       int64
	}{
		{workers: 4, items: 40, wantPeak: 4},
		{workers: 1, items: 10, wantPeak: 1},
		{workers: 16, items: 5, wantPeak: 5},
	}
	for _, tc := range cases {
		t.Run(fmt.Sprintf("workers=%d/itens=%d", tc.workers, tc.items), func(t *testing.T) {
			var p probe
			o := run(t, context.Background(), tc.workers, seq(tc.items), func(ctx context.Context, i int) (int, error) {
				p.enter()
				defer p.leave()
				time.Sleep(20 * time.Millisecond)
				return i, nil
			})
			if o.err != nil {
				t.Fatalf("erro inesperado: %v", o.err)
			}
			if got := p.peak.Load(); got > int64(tc.workers) {
				t.Errorf("pico de %d chamadas simultâneas de fn; o limite era workers=%d", got, tc.workers)
			} else if got != tc.wantPeak {
				t.Errorf("pico de %d chamadas simultâneas; com %d itens de 20ms e workers=%d esperava-se usar %d vagas",
					got, tc.items, tc.workers, tc.wantPeak)
			}
			if c := p.calls.Load(); c != int64(tc.items) {
				t.Errorf("fn foi chamada %d vez(es) para %d itens", c, tc.items)
			}
		})
	}
}

func TestPreservesOrder(t *testing.T) {
	const n = 200
	o := run(t, context.Background(), 8, seq(n), func(ctx context.Context, i int) (string, error) {
		// Itens do começo demoram mais, então terminam fora de ordem.
		time.Sleep(time.Duration((n-i)%7) * time.Millisecond)
		return fmt.Sprintf("thumb-%03d.webp", i), nil
	})
	if o.err != nil {
		t.Fatalf("erro inesperado: %v", o.err)
	}
	if len(o.results) != n {
		t.Fatalf("esperado %d resultados, vieram %d", n, len(o.results))
	}
	wrong := 0
	for i, r := range o.results {
		if want := fmt.Sprintf("thumb-%03d.webp", i); r != want {
			if wrong < 3 {
				t.Errorf("results[%d] = %q, esperado %q", i, r, want)
			}
			wrong++
		}
	}
	if wrong > 0 {
		t.Errorf("%d de %d resultados fora da posição da entrada correspondente", wrong, n)
	}
}

func TestFirstError(t *testing.T) {
	t.Run("um item falha", func(t *testing.T) {
		const workers = 4
		var p probe
		var sawCancel atomic.Int64
		o := run(t, context.Background(), workers, seq(50), func(ctx context.Context, i int) (int, error) {
			p.enter()
			defer p.leave()
			if i == 3 {
				time.Sleep(10 * time.Millisecond)
				return 0, errBoom
			}
			if err := sleepCtx(ctx, 2*time.Second); err != nil {
				sawCancel.Add(1)
				return 0, err
			}
			return i, nil
		})
		if !errors.Is(o.err, errBoom) {
			t.Fatalf("esperado errors.Is(err, errBoom) — o erro do item que falhou primeiro —, veio %v", o.err)
		}
		if !strings.Contains(o.err.Error(), "item 3") {
			t.Errorf("a mensagem de erro deveria identificar o item (\"item 3\"), veio %q", o.err)
		}
		if o.results != nil {
			t.Errorf("esperado results nil junto com o erro, veio slice com %d elementos", len(o.results))
		}
		if o.elapsed > 500*time.Millisecond {
			t.Errorf("Run levou %v para devolver o erro; as chamadas em andamento deveriam ter sido canceladas", o.elapsed.Round(time.Millisecond))
		}
		if sawCancel.Load() == 0 {
			t.Errorf("nenhuma chamada em andamento viu o ctx ser cancelado depois do erro")
		}
		if c := p.calls.Load(); c > 2*workers {
			t.Errorf("fn foi chamada %d vezes; depois do primeiro erro nenhuma chamada nova deveria começar", c)
		}
	})

	t.Run("todos os itens falham", func(t *testing.T) {
		o := run(t, context.Background(), 4, seq(100), func(ctx context.Context, i int) (int, error) {
			return 0, fmt.Errorf("foto %d corrompida: %w", i, errBoom)
		})
		if !errors.Is(o.err, errBoom) {
			t.Fatalf("esperado errors.Is(err, errBoom), veio %v", o.err)
		}
		if strings.Count(o.err.Error(), "corrompida") != 1 {
			t.Errorf("esperado exatamente um erro (o primeiro), veio %q", o.err)
		}
	})
}

func TestCallerCancellation(t *testing.T) {
	t.Run("fn observa o ctx", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		time.AfterFunc(50*time.Millisecond, cancel)
		o := run(t, ctx, 4, seq(100), func(ctx context.Context, i int) (int, error) {
			return i, sleepCtx(ctx, 5*time.Second)
		})
		if !errors.Is(o.err, context.Canceled) {
			t.Fatalf("esperado errors.Is(err, context.Canceled), veio %v", o.err)
		}
		if o.results != nil {
			t.Errorf("esperado results nil junto com o erro")
		}
		if late := o.elapsed - 50*time.Millisecond; late > 200*time.Millisecond {
			t.Errorf("Run retornou %v depois do cancelamento; o limite é 200ms", late.Round(time.Millisecond))
		}
	})

	t.Run("fn ignora o ctx", func(t *testing.T) {
		// Cada chamada é curta e não olha o ctx: quem precisa parar é o pool,
		// deixando de despachar itens depois do cancelamento.
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		time.AfterFunc(50*time.Millisecond, cancel)
		var p probe
		o := run(t, ctx, 2, seq(1000), func(_ context.Context, i int) (int, error) {
			p.enter()
			defer p.leave()
			time.Sleep(5 * time.Millisecond)
			return i, nil
		})
		if !errors.Is(o.err, context.Canceled) {
			t.Fatalf("o chamador cancelou com o lote em andamento: esperado errors.Is(err, context.Canceled), veio %v (fn chamada %d vezes, pico %d)",
				o.err, p.calls.Load(), p.peak.Load())
		}
		if c := p.calls.Load(); c > 200 {
			t.Errorf("fn foi chamada %d vezes; após o cancelamento nenhum item novo deveria ser despachado", c)
		}
		if late := o.elapsed - 50*time.Millisecond; late > 200*time.Millisecond {
			t.Errorf("Run retornou %v depois do cancelamento; o limite é 200ms", late.Round(time.Millisecond))
		}
	})

	t.Run("deadline", func(t *testing.T) {
		ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
		defer cancel()
		o := run(t, ctx, 3, seq(30), func(ctx context.Context, i int) (int, error) {
			return i, sleepCtx(ctx, 5*time.Second)
		})
		if !errors.Is(o.err, context.DeadlineExceeded) {
			t.Fatalf("esperado errors.Is(err, context.DeadlineExceeded), veio %v", o.err)
		}
	})

	t.Run("ctx já cancelado", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		cancel()
		var p probe
		o := run(t, ctx, 4, seq(10), func(ctx context.Context, i int) (int, error) {
			p.enter()
			defer p.leave()
			return i, nil
		})
		if !errors.Is(o.err, context.Canceled) {
			t.Errorf("esperado errors.Is(err, context.Canceled), veio %v", o.err)
		}
		if c := p.calls.Load(); c != 0 {
			t.Errorf("fn foi chamada %d vez(es) com um ctx que já chegou cancelado", c)
		}
	})
}

func TestNoGoroutineLeak(t *testing.T) {
	t.Run("sucesso", func(t *testing.T) {
		base := goroutines()
		run(t, context.Background(), 4, seq(50), func(ctx context.Context, i int) (int, error) {
			time.Sleep(time.Millisecond)
			return i, nil
		})
		expectNoLeak(t, base)
	})

	t.Run("erro com chamadas em andamento", func(t *testing.T) {
		base := goroutines()
		run(t, context.Background(), 4, seq(50), func(ctx context.Context, i int) (int, error) {
			if i == 1 {
				return 0, errBoom
			}
			return i, sleepCtx(ctx, 3*time.Second)
		})
		expectNoLeak(t, base)
	})

	t.Run("vários erros", func(t *testing.T) {
		base := goroutines()
		run(t, context.Background(), 8, seq(100), func(ctx context.Context, i int) (int, error) {
			return 0, errBoom
		})
		expectNoLeak(t, base)
	})

	t.Run("cancelamento do chamador", func(t *testing.T) {
		base := goroutines()
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		time.AfterFunc(20*time.Millisecond, cancel)
		run(t, ctx, 4, seq(100), func(ctx context.Context, i int) (int, error) {
			return i, sleepCtx(ctx, 3*time.Second)
		})
		expectNoLeak(t, base)
	})
}

func TestStress(t *testing.T) {
	const n = 2000
	o := run(t, context.Background(), 8, seq(n), func(ctx context.Context, i int) (int, error) {
		runtime.Gosched()
		return i * 2, nil
	})
	if o.err != nil {
		t.Fatalf("erro inesperado: %v", o.err)
	}
	if len(o.results) != n {
		t.Fatalf("esperado %d resultados, vieram %d (resultados perdidos)", n, len(o.results))
	}
	for i, r := range o.results {
		if r != i*2 {
			t.Fatalf("results[%d] = %d, esperado %d", i, r, i*2)
		}
	}
}
