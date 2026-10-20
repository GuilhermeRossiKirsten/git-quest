// Package payments contém o consumidor de PaymentRequested e o relay da outbox.
package payments

import (
	"context"
	"errors"
	"fmt"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"

	"gitquest/payments/internal/events"
)

// Consumer cobra o cliente para cada PaymentRequested recebido da fila e
// avisa o resto da empresa publicando PaymentCompleted (ou PaymentDeclined,
// quando não há saldo).
//
// Handle é chamado pela infraestrutura de fila; se ele devolver erro, a
// mensagem volta para a fila e será entregue de novo. Vários consumidores
// (processos) rodam em paralelo sobre a mesma fila.
type Consumer struct {
	db  *pgxpool.Pool
	pub events.Publisher
}

// NewConsumer cria um consumidor.
func NewConsumer(db *pgxpool.Pool, pub events.Publisher) *Consumer {
	return &Consumer{db: db, pub: pub}
}

// Handle processa um PaymentRequested.
func (c *Consumer) Handle(ctx context.Context, m events.PaymentRequested) error {
	// A fila às vezes entrega a mesma mensagem duas vezes; pulamos as que já
	// foram tratadas.
	done, err := c.alreadyProcessed(ctx, m.MessageID)
	if err != nil {
		return err
	}
	if done {
		return nil
	}

	var balance int64
	err = c.db.QueryRow(ctx, `SELECT balance_cents FROM accounts WHERE id = $1`, m.AccountID).Scan(&balance)
	if errors.Is(err, pgx.ErrNoRows) {
		return fmt.Errorf("conta %s não existe", m.AccountID)
	}
	if err != nil {
		return fmt.Errorf("lendo saldo de %s: %w", m.AccountID, err)
	}

	if balance < m.AmountCents {
		if err := c.pub.Publish(ctx, newEvent(events.TypePaymentDeclined, m)); err != nil {
			return fmt.Errorf("publicando recusa de %s: %w", m.MessageID, err)
		}
		return c.markProcessed(ctx, m.MessageID)
	}

	if err := c.charge(ctx, m, balance-m.AmountCents); err != nil {
		return err
	}

	// O time de pedidos libera a entrega assim que recebe a confirmação, então
	// publicamos direto no broker, sem esperar nenhum job.
	if err := c.pub.Publish(ctx, newEvent(events.TypePaymentCompleted, m)); err != nil {
		return fmt.Errorf("publicando confirmação de %s: %w", m.MessageID, err)
	}
	return c.markProcessed(ctx, m.MessageID)
}

// charge grava o novo saldo e a linha do extrato numa transação.
func (c *Consumer) charge(ctx context.Context, m events.PaymentRequested, newBalance int64) error {
	tx, err := c.db.Begin(ctx)
	if err != nil {
		return err
	}
	defer tx.Rollback(ctx) //nolint:errcheck // sem efeito depois do Commit

	if _, err := tx.Exec(ctx, `UPDATE accounts SET balance_cents = $1 WHERE id = $2`,
		newBalance, m.AccountID); err != nil {
		return fmt.Errorf("atualizando saldo de %s: %w", m.AccountID, err)
	}
	if _, err := tx.Exec(ctx, `INSERT INTO charges (message_id, account_id, amount_cents) VALUES ($1, $2, $3)`,
		m.MessageID, m.AccountID, m.AmountCents); err != nil {
		return fmt.Errorf("gravando extrato de %s: %w", m.MessageID, err)
	}
	return tx.Commit(ctx)
}

func (c *Consumer) alreadyProcessed(ctx context.Context, messageID string) (bool, error) {
	var exists bool
	err := c.db.QueryRow(ctx,
		`SELECT EXISTS (SELECT 1 FROM processed_messages WHERE message_id = $1)`, messageID).Scan(&exists)
	if err != nil {
		return false, fmt.Errorf("consultando processed_messages: %w", err)
	}
	return exists, nil
}

func (c *Consumer) markProcessed(ctx context.Context, messageID string) error {
	if _, err := c.db.Exec(ctx, `INSERT INTO processed_messages (message_id) VALUES ($1)`, messageID); err != nil {
		return fmt.Errorf("registrando %s como processada: %w", messageID, err)
	}
	return nil
}

// newEvent monta o evento de resultado do pagamento. O ID é derivado do
// MessageID, então o mesmo pagamento gera sempre o mesmo ID de evento.
func newEvent(typ string, m events.PaymentRequested) events.Event {
	return events.Event{
		ID:          "evt-" + m.MessageID,
		Type:        typ,
		PaymentID:   m.MessageID,
		AccountID:   m.AccountID,
		AmountCents: m.AmountCents,
	}
}
