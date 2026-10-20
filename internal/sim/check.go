package sim

import (
	"context"
	"fmt"
	"sort"

	"github.com/jackc/pgx/v5/pgxpool"

	"gitquest/payments/internal/events"
)

// Report é o resultado da conferência de um cenário.
type Report struct {
	Payments, Completed, Declined int
	LostEvents                    int // pagamentos sem nenhum evento
	DuplicateEvents               int // pagamentos com 2+ eventos de IDs diferentes
	DoubleCharges                 int // pagamentos cobrados mais de uma vez
	BalanceMismatches             int // contas cujo saldo não bate
	NegativeBalances              int
	RawPublishes, UniqueEvents    int
	Violations                    []string
}

// OK informa se nenhuma regra foi violada.
func (r Report) OK() bool { return len(r.Violations) == 0 }

func (r *Report) violate(format string, args ...any) {
	r.Violations = append(r.Violations, fmt.Sprintf(format, args...))
}

// Check confere o estado final do banco e os eventos aceitos pelo broker
// contra os pedidos do cenário. Regras (o que o downstream e a contabilidade
// esperam):
//
//   - cada pagamento tem exatamente UM evento (após deduplicar por Event.ID):
//     PaymentCompleted ou PaymentDeclined, com conta e valor do pedido;
//   - pagamento confirmado tem exatamente uma linha em charges; recusado, nenhuma;
//   - saldo final = saldo inicial − pagamentos confirmados = saldo inicial − extrato;
//   - nenhum saldo negativo;
//   - recusa só com saldo insuficiente (o saldo final fica abaixo do valor recusado).
func Check(ctx context.Context, db *pgxpool.Pool, accounts []Account, msgs []events.PaymentRequested, published []events.Event) (Report, error) {
	var r Report
	r.RawPublishes = len(published)

	byID := map[string]events.Event{}
	for _, e := range published {
		if old, ok := byID[e.ID]; ok && old != e {
			r.violate("evento %s publicado com conteúdos diferentes: %+v e %+v", e.ID, old, e)
		}
		byID[e.ID] = e
	}
	r.UniqueEvents = len(byID)
	perPayment := map[string][]events.Event{}
	for _, e := range byID {
		perPayment[e.PaymentID] = append(perPayment[e.PaymentID], e)
	}

	charges := map[string][]int64{}
	ledger := map[string]int64{}
	rows, err := db.Query(ctx, `SELECT message_id, account_id, amount_cents FROM charges`)
	if err != nil {
		return r, fmt.Errorf("lendo charges: %w", err)
	}
	for rows.Next() {
		var msgID, acc string
		var amount int64
		if err := rows.Scan(&msgID, &acc, &amount); err != nil {
			return r, err
		}
		charges[msgID] = append(charges[msgID], amount)
		ledger[acc] += amount
	}
	if err := rows.Err(); err != nil {
		return r, fmt.Errorf("lendo charges: %w", err)
	}

	final := map[string]int64{}
	rows, err = db.Query(ctx, `SELECT id, balance_cents FROM accounts`)
	if err != nil {
		return r, fmt.Errorf("lendo accounts: %w", err)
	}
	for rows.Next() {
		var id string
		var bal int64
		if err := rows.Scan(&id, &bal); err != nil {
			return r, err
		}
		final[id] = bal
	}
	if err := rows.Err(); err != nil {
		return r, fmt.Errorf("lendo accounts: %w", err)
	}

	known := map[string]bool{}
	confirmed := map[string]int64{}
	var declined []events.PaymentRequested
	for _, m := range msgs {
		if known[m.MessageID] {
			continue
		}
		known[m.MessageID] = true
		r.Payments++
		evs := perPayment[m.MessageID]
		nCharges := len(charges[m.MessageID])
		if nCharges > 1 {
			r.DoubleCharges++
			r.violate("pagamento %s: cobrado %d vezes (charges)", m.MessageID, nCharges)
		}
		switch {
		case len(evs) == 0:
			r.LostEvents++
			r.violate("pagamento %s: nenhum evento publicado (evento perdido; cobranças no extrato: %d)", m.MessageID, nCharges)
			continue
		case len(evs) > 1:
			r.DuplicateEvents++
			r.violate("pagamento %s: %d eventos com IDs diferentes — o downstream veria %d pagamentos", m.MessageID, len(evs), len(evs))
			continue
		}
		e := evs[0]
		if e.AccountID != m.AccountID || e.AmountCents != m.AmountCents {
			r.violate("pagamento %s: evento com conta/valor %s/%d, pedido era %s/%d",
				m.MessageID, e.AccountID, e.AmountCents, m.AccountID, m.AmountCents)
		}
		switch e.Type {
		case events.TypePaymentCompleted:
			r.Completed++
			confirmed[m.AccountID] += m.AmountCents
			if nCharges == 0 {
				r.violate("pagamento %s: PaymentCompleted publicado, mas nenhuma cobrança no extrato", m.MessageID)
			}
		case events.TypePaymentDeclined:
			r.Declined++
			declined = append(declined, m)
			if nCharges > 0 {
				r.violate("pagamento %s: PaymentDeclined publicado, mas o cliente foi cobrado", m.MessageID)
			}
		default:
			r.violate("pagamento %s: tipo de evento desconhecido %q", m.MessageID, e.Type)
		}
	}
	for pid := range perPayment {
		if !known[pid] {
			r.violate("evento para pagamento inexistente %q", pid)
		}
	}

	for _, a := range accounts {
		bal, ok := final[a.ID]
		if !ok {
			r.violate("conta %s sumiu da tabela accounts", a.ID)
			continue
		}
		if bal < 0 {
			r.NegativeBalances++
			r.violate("conta %s: saldo negativo (%d)", a.ID, bal)
		}
		want := a.InitialCents - confirmed[a.ID]
		fromLedger := a.InitialCents - ledger[a.ID]
		if bal != want || bal != fromLedger {
			r.BalanceMismatches++
			r.violate("conta %s: saldo final %d; esperado %d pelos pagamentos confirmados e %d pelo extrato (inicial %d)",
				a.ID, bal, want, fromLedger, a.InitialCents)
		}
	}
	for _, m := range declined {
		if bal, ok := final[m.AccountID]; ok && bal >= m.AmountCents {
			r.violate("pagamento %s: recusado por saldo insuficiente, mas a conta %s terminou com %d ≥ %d",
				m.MessageID, m.AccountID, bal, m.AmountCents)
		}
	}
	sort.Strings(r.Violations)
	return r, nil
}
