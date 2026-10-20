package sim

import (
	"context"
	"fmt"
	"math/rand/v2"
	"sync"

	"gitquest/payments/internal/events"
)

// Delivery é uma entrega de mensagem pela fila. A mesma mensagem pode gerar
// várias entregas (cópias) e cada nova tentativa incrementa Attempt.
type Delivery struct {
	Msg     events.PaymentRequested
	Copy    int // 0 = primeira cópia enfileirada desta mensagem
	Attempt int // 1 = primeira tentativa desta cópia
}

// Queue é a fila "payments.requested" em memória, com semântica at-least-once:
// uma entrega só sai da fila quando o consumidor confirma (Ack); se ele falhar
// ou morrer, ela volta para o fim da fila.
type Queue struct {
	mu       sync.Mutex
	cond     *sync.Cond
	pending  []Delivery
	inflight int
}

// NewQueue cria uma fila com as entregas na ordem dada.
func NewQueue(deliveries []Delivery) *Queue {
	q := &Queue{pending: append([]Delivery(nil), deliveries...)}
	q.cond = sync.NewCond(&q.mu)
	return q
}

// Next bloqueia até haver uma entrega. Devolve false quando a fila esvaziou e
// nenhuma entrega está em andamento, ou quando ctx é cancelado.
func (q *Queue) Next(ctx context.Context) (Delivery, bool) {
	stop := context.AfterFunc(ctx, func() {
		q.mu.Lock()
		defer q.mu.Unlock()
		q.cond.Broadcast()
	})
	defer stop()
	q.mu.Lock()
	defer q.mu.Unlock()
	for len(q.pending) == 0 {
		if q.inflight == 0 || ctx.Err() != nil {
			return Delivery{}, false
		}
		q.cond.Wait()
	}
	if ctx.Err() != nil {
		return Delivery{}, false
	}
	d := q.pending[0]
	q.pending = q.pending[1:]
	q.inflight++
	return d, true
}

// Ack confirma a entrega: ela não será entregue de novo.
func (q *Queue) Ack(Delivery) { q.done(nil) }

// Retry devolve a entrega ao fim da fila para uma nova tentativa.
func (q *Queue) Retry(d Delivery) {
	d.Attempt++
	q.done(&d)
}

func (q *Queue) done(again *Delivery) {
	q.mu.Lock()
	defer q.mu.Unlock()
	q.inflight--
	if again != nil {
		q.pending = append(q.pending, *again)
	}
	q.cond.Broadcast()
}

// Schedule gera as entregas de msgs: cada mensagem aparece de 1 a maxCopies
// vezes e a ordem é embaralhada localmente (janela window), de modo que as
// cópias de uma mensagem fiquem próximas — e cheguem a consumidores diferentes
// ao mesmo tempo. Mesma semente, mesmo cronograma.
func Schedule(msgs []events.PaymentRequested, seed uint64, maxCopies, window int) []Delivery {
	rng := rand.New(rand.NewPCG(seed, 0x20))
	var out []Delivery
	for _, m := range msgs {
		n := 1 + rng.IntN(maxCopies)
		for c := 0; c < n; c++ {
			out = append(out, Delivery{Msg: m, Copy: c, Attempt: 1})
		}
	}
	for i := range out {
		w := min(window, len(out)-i)
		j := i + rng.IntN(w)
		out[i], out[j] = out[j], out[i]
	}
	return out
}

// Account é uma conta com o saldo inicial do cenário.
type Account struct {
	ID           string
	InitialCents int64
}

// Messages gera n pedidos de pagamento determinísticos distribuídos entre as contas.
func Messages(seed uint64, n int, accounts []Account, minCents, maxCents int64) []events.PaymentRequested {
	rng := rand.New(rand.NewPCG(seed, 0x21))
	out := make([]events.PaymentRequested, n)
	for i := range out {
		out[i] = events.PaymentRequested{
			MessageID:   fmt.Sprintf("pay-%04d", i+1),
			AccountID:   accounts[rng.IntN(len(accounts))].ID,
			AmountCents: minCents + rng.Int64N(maxCents-minCents+1),
		}
	}
	return out
}
