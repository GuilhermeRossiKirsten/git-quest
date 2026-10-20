// Command orders é o serviço HTTP de pedidos.
//
// Configuração por variáveis de ambiente (valores padrão entre parênteses):
//
//	ADDR              endereço de escuta (":8080"); use "127.0.0.1:0" para uma porta livre
//	JOURNAL_PATH      arquivo do journal ("orders.journal")
//	WORK_DELAY        duração do trabalho de cada pedido ("300ms")
//	REQUEST_TIMEOUT   prazo de cada requisição ("2s")
//	FLUSH_INTERVAL    intervalo de descarga do journal ("200ms")
//	DRAIN_DELAY       espera entre sair do balanceador e parar de aceitar conexões ("2s")
//	SHUTDOWN_TIMEOUT  prazo para as requisições em andamento terminarem ("5s")
//
// Os logs saem em JSON (log/slog) na saída padrão; ao subir, o serviço registra
// {"msg":"listening","addr":"<endereço real>"}.
package main

import (
	"context"
	"log"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"desafio17/src/httpx"
	"desafio17/src/orders"
)

type config struct {
	Addr            string
	JournalPath     string
	WorkDelay       time.Duration
	RequestTimeout  time.Duration
	FlushInterval   time.Duration
	DrainDelay      time.Duration
	ShutdownTimeout time.Duration
}

func loadConfig() config {
	return config{
		Addr:            env("ADDR", ":8080"),
		JournalPath:     env("JOURNAL_PATH", "orders.journal"),
		WorkDelay:       envDuration("WORK_DELAY", 300*time.Millisecond),
		RequestTimeout:  envDuration("REQUEST_TIMEOUT", 2*time.Second),
		FlushInterval:   envDuration("FLUSH_INTERVAL", 200*time.Millisecond),
		DrainDelay:      envDuration("DRAIN_DELAY", 2*time.Second),
		ShutdownTimeout: envDuration("SHUTDOWN_TIMEOUT", 5*time.Second),
	}
}

func env(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

func envDuration(key string, def time.Duration) time.Duration {
	v := os.Getenv(key)
	if v == "" {
		return def
	}
	d, err := time.ParseDuration(v)
	if err != nil {
		log.Fatalf("%s inválido: %v", key, err)
	}
	return d
}

func main() {
	cfg := loadConfig()
	logger := slog.New(slog.NewJSONHandler(os.Stdout, nil))

	journal, err := orders.OpenJournal(cfg.JournalPath)
	if err != nil {
		log.Fatal(err)
	}
	journal.Run(context.Background(), cfg.FlushInterval)

	svc := &orders.Service{Journal: journal, WorkDelay: cfg.WorkDelay, Logger: logger}

	mux := http.NewServeMux()
	mux.HandleFunc("POST /orders", svc.CreateOrder)
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte("ok"))
	})
	mux.HandleFunc("GET /readyz", func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte("ready"))
	})

	handler := httpx.RequestID(
		httpx.Logging(logger,
			httpx.Recover(logger,
				httpx.Timeout(cfg.RequestTimeout, mux))))

	ln, err := net.Listen("tcp", cfg.Addr)
	if err != nil {
		log.Fatal(err)
	}
	logger.Info("listening", "addr", ln.Addr().String())

	srv := &http.Server{Handler: handler}

	sig := make(chan os.Signal)
	signal.Notify(sig, syscall.SIGTERM, os.Interrupt)
	go func() {
		s := <-sig
		logger.Info("shutting down", "signal", s.String())
		srv.Shutdown(context.Background())
		journal.Close()
	}()

	if err := srv.Serve(ln); err != nil {
		log.Fatal(err)
	}
}
