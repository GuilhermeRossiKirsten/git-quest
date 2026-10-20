// Package httpx reúne os middlewares HTTP do serviço de pedidos.
package httpx

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"net/http"
)

// HeaderRequestID é o cabeçalho usado para correlacionar logs entre serviços.
const HeaderRequestID = "X-Request-ID"

type ctxKey struct{}

// RequestIDFrom devolve o id da requisição guardado no contexto ("" se não houver).
func RequestIDFrom(ctx context.Context) string {
	id, _ := ctx.Value(ctxKey{}).(string)
	return id
}

// RequestID garante que toda requisição tenha um id: reaproveita o
// X-Request-ID recebido (se for válido) ou gera um novo. O id fica no
// contexto da requisição e volta para o cliente no cabeçalho da resposta.
func RequestID(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		id := r.Header.Get(HeaderRequestID)
		if !validRequestID(id) {
			id = newRequestID()
		}
		ctx := context.WithValue(r.Context(), ctxKey{}, id)
		next.ServeHTTP(w, r.WithContext(ctx))
		w.Header().Set(HeaderRequestID, id)
	})
}

// validRequestID aceita de 1 a 64 caracteres [A-Za-z0-9._-]: o id vai para
// logs e cabeçalhos, então nada de espaços, quebras de linha ou textos enormes.
func validRequestID(id string) bool {
	if len(id) == 0 || len(id) > 64 {
		return false
	}
	for _, c := range id {
		switch {
		case c >= 'a' && c <= 'z', c >= 'A' && c <= 'Z', c >= '0' && c <= '9',
			c == '-', c == '_', c == '.':
		default:
			return false
		}
	}
	return true
}

func newRequestID() string {
	var b [8]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}
