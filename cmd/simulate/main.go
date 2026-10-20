// Command simulate roda um "dia de produção" contra o PostgreSQL do compose:
// fila at-least-once com reentregas, quatro consumidores, broker instável e
// algumas quedas de processo. No fim, confere extrato, saldos e eventos.
//
// O banco payments_sim é recriado a cada execução e fica disponível para
// inspeção com psql (veja o README).
package main

import (
	"context"
	"fmt"
	"os"
	"time"

	"gitquest/payments/internal/migrate"
	"gitquest/payments/internal/payments"
	"gitquest/payments/internal/sim"
)

const dbName = "payments_sim"

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, "simulate:", err)
		os.Exit(1)
	}
}

func run() error {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()
	base := os.Getenv("DATABASE_URL")
	if base == "" {
		return fmt.Errorf("DATABASE_URL não definida")
	}

	admin, err := sim.Connect(ctx, base, "", nil, 2)
	if err != nil {
		return err
	}
	defer admin.Close()
	if err := sim.RecreateDatabase(ctx, admin, dbName); err != nil {
		return err
	}
	db, err := sim.Connect(ctx, base, dbName, &sim.Tracer{Latency: time.Millisecond}, 20)
	if err != nil {
		return err
	}
	defer db.Close()
	if _, err := migrate.Apply(ctx, db, "migrations"); err != nil {
		return err
	}

	accounts := []sim.Account{{ID: "ana", InitialCents: 50000}, {ID: "bruno", InitialCents: 30000}, {ID: "carla", InitialCents: 20000}}
	msgs := sim.Messages(2020, 300, accounts, 500, 2500)
	if err := sim.SeedAccounts(ctx, db, accounts); err != nil {
		return err
	}
	broker := &sim.Broker{FailEvery: 12}

	fmt.Printf("Cenário: %d pedidos de pagamento, 3 contas, 4 consumidores, fila com reentregas,\n", len(msgs))
	fmt.Println("         broker instável e ~1 queda de consumidor a cada 25 entregas.")
	cs := sim.RunConsumers(ctx, sim.NewQueue(sim.Schedule(msgs, 2020, 3, 8)), sim.ConsumerConfig{
		Workers:    4,
		NewHandler: func() sim.Handler { return payments.NewConsumer(db, broker) },
		Fault: func(d sim.Delivery) *sim.Fault {
			n := 0
			fmt.Sscanf(d.Msg.MessageID, "pay-%d", &n)
			if d.Attempt == 1 && d.Copy == 0 && n%25 == 0 {
				return &sim.Fault{CrashAt: n%9 + 1}
			}
			return nil
		},
	})
	rs, relayErr := sim.DrainRelay(ctx, func() sim.Relay { return payments.NewRelay(db, broker) }, nil, 400)
	fmt.Printf("Entregas: %d · quedas: %d · erros devolvidos à fila: %d · publicações pelo relay: %d\n",
		cs.Deliveries, cs.Crashes, cs.Errors, rs.Published)
	if relayErr != nil {
		fmt.Println("relay:", relayErr)
	}

	rep, err := sim.Check(ctx, db, accounts, msgs, broker.Accepted())
	if err != nil {
		return err
	}
	fmt.Println()
	fmt.Printf("  pagamentos ............................ %d\n", rep.Payments)
	fmt.Printf("  confirmados / recusados ............... %d / %d\n", rep.Completed, rep.Declined)
	fmt.Printf("  publicações aceitas pelo broker ....... %d (%d eventos distintos)\n", rep.RawPublishes, rep.UniqueEvents)
	fmt.Printf("  pagamentos sem evento (perdidos) ...... %d\n", rep.LostEvents)
	fmt.Printf("  pagamentos com eventos distintos ...... %d\n", rep.DuplicateEvents)
	fmt.Printf("  pagamentos cobrados mais de uma vez ... %d\n", rep.DoubleCharges)
	fmt.Printf("  contas com saldo que não bate ......... %d\n", rep.BalanceMismatches)
	fmt.Printf("  contas com saldo negativo ............. %d\n", rep.NegativeBalances)
	fmt.Println()
	if rep.OK() {
		fmt.Println("Nenhuma inconsistência encontrada neste cenário.")
		return nil
	}
	fmt.Printf("%d inconsistência(s). Algumas:\n", len(rep.Violations))
	for i, v := range rep.Violations {
		if i == 10 {
			break
		}
		fmt.Println("  -", v)
	}
	return nil
}
