package sim

import (
	"context"
	"fmt"
	"sync"

	"gitquest/payments/internal/events"
)

// Handler é um processo consumidor de PaymentRequested.
type Handler interface {
	Handle(ctx context.Context, m events.PaymentRequested) error
}

// Relay é um processo que publica a outbox no broker.
type Relay interface {
	RunOnce(ctx context.Context) (int, error)
}

// ConsumerConfig descreve como os consumidores rodam.
type ConsumerConfig struct {
	Workers     int
	NewHandler  func() Handler        // "sobe" um processo consumidor novo
	Fault       func(Delivery) *Fault // queda planejada para a entrega (ou nil)
	MaxAttempts int                   // tentativas por entrega antes de desistir (0 = 25)
}

// ConsumerStats resume a execução.
type ConsumerStats struct {
	Deliveries, Crashes, Errors int
	GaveUp                      []string // mensagens abandonadas, com o último erro
}

// RunConsumers processa a fila até esvaziá-la, com Workers consumidores em
// paralelo. Um consumidor que "morre" é substituído por um novo (NewHandler) e
// a entrega volta para a fila, sem ack.
func RunConsumers(ctx context.Context, q *Queue, cfg ConsumerConfig) ConsumerStats {
	maxAttempts := cfg.MaxAttempts
	if maxAttempts == 0 {
		maxAttempts = 25
	}
	var (
		mu    sync.Mutex
		stats ConsumerStats
		wg    sync.WaitGroup
	)
	for w := 0; w < max(cfg.Workers, 1); w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			h := cfg.NewHandler()
			for {
				d, ok := q.Next(ctx)
				if !ok {
					return
				}
				var f *Fault
				if cfg.Fault != nil {
					f = cfg.Fault(d)
				}
				hctx := ctx
				if f != nil {
					hctx = WithFault(ctx, f)
				}
				err := safeHandle(hctx, h, d.Msg)

				mu.Lock()
				stats.Deliveries++
				switch {
				case f != nil && f.CrashAt > 0:
					// Morreu no passo planejado ou, se terminou antes, logo
					// depois de terminar — antes do ack.
					stats.Crashes++
					mu.Unlock()
					h = cfg.NewHandler()
					q.Retry(d)
				case err != nil && d.Attempt >= maxAttempts:
					stats.Errors++
					stats.GaveUp = append(stats.GaveUp, fmt.Sprintf("%s: %v", d.Msg.MessageID, err))
					mu.Unlock()
					q.Ack(d)
				case err != nil:
					stats.Errors++
					mu.Unlock()
					q.Retry(d)
				default:
					mu.Unlock()
					q.Ack(d)
				}
			}
		}()
	}
	wg.Wait()
	return stats
}

func safeHandle(ctx context.Context, h Handler, m events.PaymentRequested) (err error) {
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("panic: %v", r)
		}
	}()
	return h.Handle(ctx, m)
}

// RelayStats resume a drenagem da outbox.
type RelayStats struct {
	Runs, Crashes, Errors, Published int
	LastErr                          error
}

// DrainRelay executa RunOnce até uma execução bem-sucedida não publicar nada.
// fault(run) pode planejar a queda do relay numa execução; o relay morto é
// substituído por um novo (newRelay).
func DrainRelay(ctx context.Context, newRelay func() Relay, fault func(run int) *Fault, maxRuns int) (RelayStats, error) {
	var st RelayStats
	r := newRelay()
	for st.Runs < maxRuns {
		st.Runs++
		var f *Fault
		if fault != nil {
			f = fault(st.Runs)
		}
		rctx := ctx
		if f != nil {
			rctx = WithFault(ctx, f)
		}
		n, err := safeRun(rctx, r)
		switch {
		case f != nil && f.CrashAt > 0:
			st.Crashes++
			r = newRelay()
		case err != nil:
			st.Errors++
			st.LastErr = err
			st.Published += n
		case n == 0:
			return st, nil
		default:
			st.Published += n
		}
	}
	return st, fmt.Errorf("a outbox não esvaziou após %d execuções do relay (último erro: %v)", maxRuns, st.LastErr)
}

func safeRun(ctx context.Context, r Relay) (n int, err error) {
	defer func() {
		if p := recover(); p != nil {
			err = fmt.Errorf("panic: %v", p)
		}
	}()
	return r.RunOnce(ctx)
}
