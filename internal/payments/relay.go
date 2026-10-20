package payments

import (
	"context"
	"encoding/json"
	"fmt"

	"github.com/jackc/pgx/v5/pgxpool"

	"gitquest/payments/internal/events"
)

// Relay publica no broker os eventos gravados na tabela outbox (ver
// migrations/001_init.sql). Roda como um processo separado, em laço: cada
// chamada a RunOnce trata os eventos pendentes naquele momento.
type Relay struct {
	db        *pgxpool.Pool
	pub       events.Publisher
	batchSize int
}

// NewRelay cria um relay.
func NewRelay(db *pgxpool.Pool, pub events.Publisher) *Relay {
	return &Relay{db: db, pub: pub, batchSize: 100}
}

// RunOnce publica um lote de eventos pendentes e devolve quantos publicou.
func (r *Relay) RunOnce(ctx context.Context) (int, error) {
	// Reservamos o lote marcando-o como publicado ANTES de enviar: assim dois
	// relays nunca pegam o mesmo evento e nada é publicado em dobro.
	rows, err := r.db.Query(ctx, `
		UPDATE outbox SET published_at = now()
		 WHERE id IN (SELECT id FROM outbox WHERE published_at IS NULL ORDER BY id LIMIT $1)
		RETURNING payload`, r.batchSize)
	if err != nil {
		return 0, fmt.Errorf("lendo outbox: %w", err)
	}
	var batch []events.Event
	for rows.Next() {
		var payload []byte
		if err := rows.Scan(&payload); err != nil {
			rows.Close()
			return 0, err
		}
		var e events.Event
		if err := json.Unmarshal(payload, &e); err != nil {
			rows.Close()
			return 0, fmt.Errorf("payload inválido na outbox: %w", err)
		}
		batch = append(batch, e)
	}
	if err := rows.Err(); err != nil {
		return 0, fmt.Errorf("lendo outbox: %w", err)
	}

	published := 0
	for _, e := range batch {
		if err := r.pub.Publish(ctx, e); err != nil {
			return published, fmt.Errorf("publicando %s: %w", e.ID, err)
		}
		published++
	}
	return published, nil
}
