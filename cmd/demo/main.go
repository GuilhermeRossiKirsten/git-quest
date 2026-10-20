// Command demo exercita fetch.Get contra um servidor HTTP local e mostra,
// para cada cenário, quanto tempo a chamada levou, quantas requisições o
// servidor recebeu e qual erro voltou. Útil para observar os sintomas.
//
//	make demo        (dentro do container — ./start.sh já executa uma vez)
package main

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"time"

	"desafio8/src/fetch"
)

type scenario struct {
	name    string
	expect  string
	handler http.HandlerFunc
	timeout time.Duration
	policy  fetch.RetryPolicy
}

func main() {
	scenarios := []scenario{
		{
			name:   "fornecedor lento, prazo de 300ms",
			expect: "retornar em ~300ms com context deadline exceeded",
			handler: func(w http.ResponseWriter, r *http.Request) {
				select {
				case <-r.Context().Done():
				case <-time.After(2 * time.Second):
				}
				io.WriteString(w, "preços")
			},
			timeout: 300 * time.Millisecond,
			policy:  fetch.RetryPolicy{MaxAttempts: 1},
		},
		{
			name:    "fornecedor fora do ar (503), prazo de 500ms",
			expect:  "desistir antes de esperar 1s que não cabem no prazo",
			handler: func(w http.ResponseWriter, r *http.Request) { http.Error(w, "manutenção", 503) },
			timeout: 500 * time.Millisecond,
			policy:  fetch.RetryPolicy{MaxAttempts: 3, BaseDelay: time.Second, MaxDelay: 5 * time.Second},
		},
		{
			name:    "recurso inexistente (404)",
			expect:  "1 requisição, erro imediato",
			handler: func(w http.ResponseWriter, r *http.Request) { http.NotFound(w, r) },
			timeout: 5 * time.Second,
			policy:  fetch.RetryPolicy{MaxAttempts: 4, BaseDelay: 100 * time.Millisecond, MaxDelay: time.Second},
		},
	}

	for i, sc := range scenarios {
		var hits atomic.Int64
		srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			hits.Add(1)
			sc.handler(w, r)
		}))

		ctx, cancel := context.WithTimeout(context.Background(), sc.timeout)
		start := time.Now()
		body, err := fetch.Get(ctx, srv.Client(), srv.URL, sc.policy)
		elapsed := time.Since(start)
		cancel()

		fmt.Printf("\n[%d] %s\n", i+1, sc.name)
		fmt.Printf("    esperado:     %s\n", sc.expect)
		fmt.Printf("    levou:        %v\n", elapsed.Round(time.Millisecond))
		fmt.Printf("    requisições:  %d\n", hits.Load())
		if err != nil {
			fmt.Printf("    erro:         %v\n", err)
		} else {
			fmt.Printf("    corpo:        %q\n", body)
		}
		srv.CloseClientConnections()
		srv.Close()
	}
}
