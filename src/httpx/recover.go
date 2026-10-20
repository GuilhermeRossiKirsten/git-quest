package httpx

import (
	"log/slog"
	"net/http"
	"runtime/debug"
)

// Recover impede que um pânico num handler derrube o serviço: registra o
// erro com a pilha e segue em frente.
func Recover(logger *slog.Logger, next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer func() {
			if p := recover(); p != nil {
				logger.ErrorContext(r.Context(), "panic",
					"panic", p,
					"path", r.URL.Path,
					"request_id", RequestIDFrom(r.Context()),
					"stack", string(debug.Stack()),
				)
			}
		}()
		next.ServeHTTP(w, r)
	})
}
