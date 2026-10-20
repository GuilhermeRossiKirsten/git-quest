// greeter — gitquest-desafio-9-source

package main

import (
	"encoding/json"
	"net/http"
	"os"
)

const defaultGreeting = "Olá do greeter!"

// greetingFromEnv lê a saudação da variável GREETING (configuração em tempo
// de execução, nunca embutida na imagem).
func greetingFromEnv() string {
	if g := os.Getenv("GREETING"); g != "" {
		return g
	}
	return defaultGreeting
}

func newMux(greeting string) *http.ServeMux {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		_, _ = w.Write([]byte("ok\n"))
	})
	mux.HandleFunc("GET /api/greeting", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, map[string]string{"message": greeting})
	})
	// /whoami mostra com qual usuário o processo está rodando.
	mux.HandleFunc("GET /whoami", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, map[string]int{"uid": os.Getuid(), "gid": os.Getgid()})
	})
	return mux
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}
