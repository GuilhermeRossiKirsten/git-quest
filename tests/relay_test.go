package tests

import (
	"context"
	"encoding/json"
	"fmt"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"

	"gitquest/payments/internal/events"
	"gitquest/payments/internal/sim"
)

// seedOutbox grava n eventos pendentes na outbox, no formato documentado em
// migrations/001_init.sql.
func seedOutbox(t *testing.T, db *pgxpool.Pool, n int) []events.Event {
	t.Helper()
	var out []events.Event
	for i := 1; i <= n; i++ {
		e := events.Event{
			ID: fmt.Sprintf("evt-seed-%03d", i), Type: events.TypePaymentCompleted,
			PaymentID: fmt.Sprintf("seed-%03d", i), AccountID: "acc-01", AmountCents: int64(i),
		}
		payload, _ := json.Marshal(e)
		if _, err := db.Exec(context.Background(),
			`INSERT INTO outbox (event_id, event_type, payload) VALUES ($1, $2, $3)`,
			e.ID, e.Type, payload); err != nil {
			t.Fatal(err)
		}
		out = append(out, e)
	}
	return out
}

// checkDelivered exige que todo evento semeado tenha chegado ao broker (ao
// menos uma vez, com o conteúdo original) e que nada tenha ficado pendente.
func checkDelivered(t *testing.T, db *pgxpool.Pool, broker *sim.Broker, want []events.Event) {
	t.Helper()
	got := map[string]events.Event{}
	for _, e := range broker.Accepted() {
		got[e.ID] = e
	}
	lost := 0
	for _, e := range want {
		g, ok := got[e.ID]
		switch {
		case !ok:
			lost++
			if lost <= 5 {
				t.Errorf("evento %s nunca chegou ao broker (perdido)", e.ID)
			}
		case g != e:
			t.Errorf("evento %s chegou alterado: %+v", e.ID, g)
		}
	}
	if lost > 5 {
		t.Errorf("... %d eventos perdidos no total", lost)
	}
	if len(got) != len(want) {
		t.Errorf("o broker recebeu %d eventos distintos, esperado %d", len(got), len(want))
	}
	var pending int
	if err := db.QueryRow(context.Background(),
		`SELECT count(*) FROM outbox WHERE published_at IS NULL`).Scan(&pending); err != nil {
		t.Fatal(err)
	}
	if pending != 0 {
		t.Errorf("%d evento(s) ainda pendentes na outbox depois de o relay dizer que terminou", pending)
	}
}

// Sem falhas: publica cada evento exatamente uma vez; rodar de novo não republica.
func TestRelay_Idempotente(t *testing.T) {
	db := newTestDB(t, 0)
	broker := &sim.Broker{}
	want := seedOutbox(t, db, 250)
	ctx := testCtx(t, 30*time.Second)
	s := &scenario{db: db, broker: broker}
	s.drainRelay(t, ctx, nil)
	checkDelivered(t, db, broker, want)
	if n := len(broker.Accepted()); n != len(want) {
		t.Errorf("sem falhas, o broker deveria receber %d publicações, recebeu %d", len(want), n)
	}
	before := len(broker.Accepted())
	n, err := s.newRelay().RunOnce(ctx)
	if err != nil || n != 0 || len(broker.Accepted()) != before {
		t.Errorf("RunOnce com a outbox já publicada: publicou %d (broker +%d), erro %v; esperado 0, nil",
			n, len(broker.Accepted())-before, err)
	}
}

// O broker falha de vez em quando e o relay morre em vários pontos (antes de
// consultar, entre publicar e marcar, ...). Republicar é aceitável (o
// downstream deduplica pelo ID); perder, não.
func TestRelay_CrashNaoPerdeEventos(t *testing.T) {
	db := newTestDB(t, 0)
	broker := &sim.Broker{FailEvery: 9}
	want := seedOutbox(t, db, 120)
	ctx := testCtx(t, 30*time.Second)
	s := &scenario{db: db, broker: broker}
	crashAt := []int{2, 3, 5, 8, 13, 21, 1, 34, 4}
	s.drainRelay(t, ctx, func(run int) *sim.Fault {
		if run <= len(crashAt) {
			return &sim.Fault{CrashAt: crashAt[run-1]}
		}
		return nil
	})
	checkDelivered(t, db, broker, want)
}
