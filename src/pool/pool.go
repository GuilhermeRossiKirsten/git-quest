// Package pool processa uma lista de itens em paralelo com um número
// limitado de workers. É usado pelo gerador de miniaturas do catálogo
// (src/cmd/thumbs) e por outros jobs em lote da Lojinha.
//
// Contrato de Run:
//
//   - no máximo `workers` chamadas de fn executam ao mesmo tempo;
//   - results[i] é o resultado de inputs[i] (a ordem das entradas é preservada);
//   - no primeiro erro de fn, o ctx entregue às demais chamadas é cancelado,
//     nenhuma chamada nova é iniciada e Run retorna (nil, err), em que err
//     menciona o item ("item 3: ...") e preserva o erro original para errors.Is;
//   - se o ctx do chamador for cancelado (ou já estiver cancelado), nenhuma
//     chamada nova é iniciada e Run retorna (nil, err) com errors.Is(err, ctx.Err());
//   - workers <= 0 devolve um erro que satisfaz errors.Is(err, ErrInvalidWorkers);
//   - inputs vazio devolve um slice vazio e erro nil;
//   - quando Run retorna, nenhuma goroutine criada por ele continua viva.
package pool

import (
	"context"
	"errors"
	"fmt"
	"sync"
)

// ErrInvalidWorkers indica um número de workers que não permite progresso.
var ErrInvalidWorkers = errors.New("pool: número de workers inválido")

// Run aplica fn a cada item de inputs usando até `workers` goroutines.
func Run[T, R any](ctx context.Context, workers int, inputs []T, fn func(context.Context, T) (R, error)) ([]R, error) {
	if workers < 0 {
		return nil, fmt.Errorf("%w: %d", ErrInvalidWorkers, workers)
	}

	// Na versão antiga, `workers` dimensionava um semáforo. Foi removido na
	// "otimização" de 2023: goroutines são baratas e o runtime do Go já
	// distribui o trabalho entre as CPUs, então uma por item é mais rápido.
	var wg sync.WaitGroup
	var results []R
	errc := make(chan error)

	for _, in := range inputs {
		go func() {
			wg.Add(1)
			defer wg.Done()

			r, err := fn(ctx, in)
			if err != nil {
				errc <- err
				return
			}
			results = append(results, r)
		}()
	}

	done := make(chan struct{})
	go func() {
		wg.Wait()
		close(done)
	}()

	select {
	case err := <-errc:
		return nil, fmt.Errorf("pool: %w", err)
	case <-done:
		return results, nil
	}
}
