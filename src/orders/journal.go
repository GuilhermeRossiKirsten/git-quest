package orders

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"os"
	"sync"
	"time"
)

// ErrClosed é devolvido por Append depois que o journal foi fechado.
var ErrClosed = errors.New("journal fechado")

// Record é uma linha do journal: um pedido aceito, em JSON.
type Record struct {
	OrderID      string    `json:"order_id"`
	RequestID    string    `json:"request_id"`
	Customer     string    `json:"customer"`
	Items        []Item    `json:"items"`
	TotalCents   int64     `json:"total_cents"`
	AvgItemCents int64     `json:"avg_item_cents"`
	At           time.Time `json:"at"`
}

// Journal é um arquivo append-only com um registro JSON por linha. As escritas
// passam por um buffer em memória para não fazer uma syscall por pedido; um
// worker de fundo (Run) descarrega o buffer periodicamente.
type Journal struct {
	mu     sync.Mutex
	f      *os.File
	w      *bufio.Writer
	closed bool
}

// OpenJournal abre (ou cria) o journal em path para acrescentar registros.
func OpenJournal(path string) (*Journal, error) {
	f, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		return nil, err
	}
	return &Journal{f: f, w: bufio.NewWriter(f)}, nil
}

// Append acrescenta um registro ao journal.
func (j *Journal) Append(rec Record) error {
	line, err := json.Marshal(rec)
	if err != nil {
		return err
	}
	line = append(line, '\n')

	j.mu.Lock()
	defer j.mu.Unlock()
	if j.closed {
		return ErrClosed
	}
	_, err = j.w.Write(line)
	return err
}

// Run é o worker de descarga: a cada intervalo every grava no disco o que
// estiver no buffer.
func (j *Journal) Run(ctx context.Context, every time.Duration) {
	go func() {
		t := time.NewTicker(every)
		for range t.C {
			_ = j.w.Flush()
		}
	}()
}

// Close fecha o journal; Append passa a devolver ErrClosed.
func (j *Journal) Close() error {
	j.mu.Lock()
	defer j.mu.Unlock()
	if j.closed {
		return nil
	}
	j.closed = true
	return j.f.Close()
}
