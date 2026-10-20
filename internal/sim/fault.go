// Package sim simula o mundo em volta do consumidor de pagamentos: a fila
// at-least-once, o broker de eventos, quedas do processo e latência de rede.
// É usado pelos testes e pela demonstração (cmd/simulate). Não faz parte do
// desafio: não altere.
package sim

import (
	"context"
	"errors"
	"sync"
	"time"

	"github.com/jackc/pgx/v5"
)

// ErrCrashed é devolvido a um processo "morto": nada do que ele tentar depois
// da queda chega ao banco nem ao broker.
var ErrCrashed = errors.New("processo encerrado (queda simulada)")

// Fault simula a queda de um processo (consumidor ou relay) no meio do trabalho.
//
// Cada efeito externo — um comando SQL ou uma publicação no broker — é um
// "passo". Com CrashAt = k, o processo morre imediatamente ANTES do k-ésimo
// passo: o comando não chega ao servidor (a conexão é fechada, então o
// PostgreSQL desfaz qualquer transação aberta) e todos os passos seguintes
// falham. Se o processo terminar antes do passo k, ele morre logo depois de
// terminar, antes de confirmar (ack) a mensagem à fila.
type Fault struct {
	CrashAt int

	mu    sync.Mutex
	steps int
	dead  bool
}

// step registra um passo e informa se o processo já está morto.
func (f *Fault) step() bool {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.steps++
	if f.CrashAt > 0 && f.steps >= f.CrashAt {
		f.dead = true
	}
	return f.dead
}

// Dead informa se a queda já aconteceu.
func (f *Fault) Dead() bool {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.dead
}

type faultKey struct{}

// WithFault associa uma queda planejada ao contexto entregue ao processo.
func WithFault(ctx context.Context, f *Fault) context.Context {
	return context.WithValue(ctx, faultKey{}, f)
}

func faultFrom(ctx context.Context) *Fault {
	f, _ := ctx.Value(faultKey{}).(*Fault)
	return f
}

// Tracer é instalado no pool do pgx: conta passos SQL para as quedas
// simuladas e, opcionalmente, acrescenta latência a cada comando (como uma
// rede real), o que torna janelas de concorrência visíveis.
type Tracer struct {
	Latency time.Duration
}

func (t *Tracer) before(ctx context.Context, conn *pgx.Conn) {
	if f := faultFrom(ctx); f != nil && f.step() {
		// Fechar o socket antes de enviar o comando é o que o servidor veria se
		// o processo morresse aqui: a sessão cai e a transação aberta é desfeita.
		_ = conn.PgConn().Conn().Close()
		return
	}
	if t.Latency > 0 {
		time.Sleep(t.Latency)
	}
}

// TraceQueryStart implementa pgx.QueryTracer.
func (t *Tracer) TraceQueryStart(ctx context.Context, conn *pgx.Conn, _ pgx.TraceQueryStartData) context.Context {
	t.before(ctx, conn)
	return ctx
}

// TraceQueryEnd implementa pgx.QueryTracer.
func (t *Tracer) TraceQueryEnd(context.Context, *pgx.Conn, pgx.TraceQueryEndData) {}

// TraceBatchStart implementa pgx.BatchTracer (um lote conta como um passo).
func (t *Tracer) TraceBatchStart(ctx context.Context, conn *pgx.Conn, _ pgx.TraceBatchStartData) context.Context {
	t.before(ctx, conn)
	return ctx
}

// TraceBatchQuery implementa pgx.BatchTracer.
func (t *Tracer) TraceBatchQuery(context.Context, *pgx.Conn, pgx.TraceBatchQueryData) {}

// TraceBatchEnd implementa pgx.BatchTracer.
func (t *Tracer) TraceBatchEnd(context.Context, *pgx.Conn, pgx.TraceBatchEndData) {}

// TraceCopyFromStart implementa pgx.CopyFromTracer (um COPY conta como um passo).
func (t *Tracer) TraceCopyFromStart(ctx context.Context, conn *pgx.Conn, _ pgx.TraceCopyFromStartData) context.Context {
	t.before(ctx, conn)
	return ctx
}

// TraceCopyFromEnd implementa pgx.CopyFromTracer.
func (t *Tracer) TraceCopyFromEnd(context.Context, *pgx.Conn, pgx.TraceCopyFromEndData) {}
