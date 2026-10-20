package httpx

import (
	"context"
	"net/http"
	"time"
)

// TimeoutBody é o corpo enviado quando a requisição estoura o prazo.
const TimeoutBody = `{"error":"timeout"}`

// Timeout limita o tempo de cada requisição: se o handler não terminar em d,
// o cliente recebe 503 com TimeoutBody. O handler recebe um contexto com esse
// prazo para poder desistir do trabalho.
func Timeout(d time.Duration, next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		ctx, cancel := context.WithTimeout(r.Context(), d)
		defer cancel()

		done := make(chan struct{})
		go func() {
			next.ServeHTTP(w, r.WithContext(ctx))
			done <- struct{}{}
		}()

		select {
		case <-done:
		case <-ctx.Done():
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusServiceUnavailable)
			_, _ = w.Write([]byte(TimeoutBody))
		}
	})
}
