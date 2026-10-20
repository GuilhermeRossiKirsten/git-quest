package tests

import (
	"context"
	"fmt"
	"os"
	"regexp"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"

	"gitquest/payments/internal/events"
	"gitquest/payments/internal/migrate"
	"gitquest/payments/internal/payments"
	"gitquest/payments/internal/sim"
)

var dbSeq atomic.Int64

func databaseURL(t *testing.T) string {
	t.Helper()
	url := os.Getenv("DATABASE_URL")
	if url == "" {
		t.Fatal("DATABASE_URL não definida — rode os testes com ./test.sh")
	}
	return url
}

// newTestDB cria um banco descartável só para este teste, aplica
// migrations/*.sql (inclusive a sua 002) e o apaga no fim.
func newTestDB(t *testing.T, latency time.Duration) *pgxpool.Pool {
	t.Helper()
	ctx := context.Background()
	base := databaseURL(t)
	admin, err := sim.Connect(ctx, base, "", nil, 2)
	if err != nil {
		t.Fatalf("conectando ao PostgreSQL: %v", err)
	}
	t.Cleanup(admin.Close)

	name := fmt.Sprintf("gq_%s_%d_%d", strings.ToLower(regexp.MustCompile(`\W+`).ReplaceAllString(t.Name(), "_")),
		os.Getpid(), dbSeq.Add(1))
	if len(name) > 60 {
		name = name[len(name)-60:]
	}
	if err := sim.RecreateDatabase(ctx, admin, name); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = sim.DropDatabase(context.Background(), admin, name) })

	db, err := sim.Connect(ctx, base, name, &sim.Tracer{Latency: latency}, 20)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(db.Close)
	if _, err := migrate.Apply(ctx, db, "../migrations"); err != nil {
		t.Fatalf("aplicando migrations/: %v", err)
	}
	return db
}

// scenario agrupa um cenário de pagamentos.
type scenario struct {
	db       *pgxpool.Pool
	broker   *sim.Broker
	accounts []sim.Account
	msgs     []events.PaymentRequested
}

func newScenario(t *testing.T, latency time.Duration, accounts []sim.Account, msgs []events.PaymentRequested, failEvery int) *scenario {
	t.Helper()
	db := newTestDB(t, latency)
	if err := sim.SeedAccounts(context.Background(), db, accounts); err != nil {
		t.Fatal(err)
	}
	return &scenario{db: db, broker: &sim.Broker{FailEvery: failEvery}, accounts: accounts, msgs: msgs}
}

func (s *scenario) newConsumer() sim.Handler { return payments.NewConsumer(s.db, s.broker) }
func (s *scenario) newRelay() sim.Relay      { return payments.NewRelay(s.db, s.broker) }

func (s *scenario) consume(t *testing.T, ctx context.Context, maxCopies int, cfg sim.ConsumerConfig) sim.ConsumerStats {
	t.Helper()
	cfg.NewHandler = s.newConsumer
	st := sim.RunConsumers(ctx, sim.NewQueue(sim.Schedule(s.msgs, 7, maxCopies, 8)), cfg)
	if ctx.Err() != nil {
		t.Fatalf("os consumidores não terminaram a fila a tempo (%v) — algo travou (deadlock? conexão presa?)", ctx.Err())
	}
	for i, g := range st.GaveUp {
		if i == 5 {
			t.Errorf("... e mais %d mensagens abandonadas", len(st.GaveUp)-5)
			break
		}
		t.Errorf("mensagem abandonada após muitas falhas: %s", g)
	}
	return st
}

func (s *scenario) drainRelay(t *testing.T, ctx context.Context, fault func(int) *sim.Fault) sim.RelayStats {
	t.Helper()
	st, err := sim.DrainRelay(ctx, s.newRelay, fault, 400)
	if err != nil {
		t.Errorf("relay: %v", err)
	}
	return st
}

// verify confere as regras de negócio (ver sim.Check) e relata as violações.
func (s *scenario) verify(t *testing.T) sim.Report {
	t.Helper()
	rep, err := sim.Check(context.Background(), s.db, s.accounts, s.msgs, s.broker.Accepted())
	if err != nil {
		t.Fatalf("conferindo o resultado: %v", err)
	}
	if !rep.OK() {
		t.Errorf("%d regra(s) violada(s): %d cobranças duplicadas, %d eventos perdidos, %d pagamentos com eventos distintos, %d saldos errados, %d saldos negativos",
			len(rep.Violations), rep.DoubleCharges, rep.LostEvents, rep.DuplicateEvents, rep.BalanceMismatches, rep.NegativeBalances)
		for i, v := range rep.Violations {
			if i == 8 {
				t.Errorf("... e mais %d", len(rep.Violations)-8)
				break
			}
			t.Error(v)
		}
	}
	return rep
}

func testCtx(t *testing.T, d time.Duration) context.Context {
	ctx, cancel := context.WithTimeout(context.Background(), d)
	t.Cleanup(cancel)
	return ctx
}

func accounts(n int, initial int64) []sim.Account {
	out := make([]sim.Account, n)
	for i := range out {
		out[i] = sim.Account{ID: fmt.Sprintf("acc-%02d", i+1), InitialCents: initial}
	}
	return out
}
