package sim

import (
	"context"
	"errors"
	"hash/fnv"
	"sync"

	"gitquest/payments/internal/events"
)

// ErrUnavailable é a falha transitória do broker simulado.
var ErrUnavailable = errors.New("broker indisponível (falha transitória simulada)")

// Broker é o tópico "payments.events" em memória. Ele guarda tudo o que
// aceitou, inclusive repetições: quem lê deduplica por Event.ID, como os
// consumidores downstream reais.
type Broker struct {
	// FailEvery > 0 faz cerca de 1 em cada FailEvery eventos falhar na primeira
	// tentativa de publicação. A escolha depende só do ID do evento, então é
	// determinística mesmo com publicações concorrentes.
	FailEvery int

	mu       sync.Mutex
	accepted []events.Event
	attempts map[string]int
}

// Publish implementa events.Publisher.
func (b *Broker) Publish(ctx context.Context, e events.Event) error {
	if f := faultFrom(ctx); f != nil && f.step() {
		return ErrCrashed
	}
	if err := ctx.Err(); err != nil {
		return err
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.attempts == nil {
		b.attempts = map[string]int{}
	}
	b.attempts[e.ID]++
	if b.FailEvery > 0 && b.attempts[e.ID] == 1 && hash(e.ID)%uint32(b.FailEvery) == 0 {
		return ErrUnavailable
	}
	b.accepted = append(b.accepted, e)
	return nil
}

// Accepted devolve uma cópia de todos os eventos aceitos, na ordem.
func (b *Broker) Accepted() []events.Event {
	b.mu.Lock()
	defer b.mu.Unlock()
	return append([]events.Event(nil), b.accepted...)
}

func hash(s string) uint32 {
	h := fnv.New32a()
	_, _ = h.Write([]byte(s))
	return h.Sum32()
}
