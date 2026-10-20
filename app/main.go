// Command inventory-api é o serviço de estoque usado no desafio-10.
//
// Este código NÃO faz parte do que você deve alterar: o problema está nos
// manifests em k8s/. Ele é versionado para que você possa entender o
// comportamento esperado do contêiner (variáveis, portas e endpoints).
//
// Modos de execução:
//
//	/app               servidor HTTP em :8080
//	/app probe <url>   faz um GET e imprime "HTTP <status>" + corpo; sai com
//	                   código 0 somente para respostas 2xx. A imagem não tem
//	                   shell nem curl, então use: kubectl exec ... -- /app probe <url>
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"strconv"
	"sync/atomic"
	"syscall"
	"time"
)

const (
	listenAddr     = ":8080"
	defaultCacheMB = 64
	pageSize       = 4096
)

type item struct {
	SKU      string `json:"sku"`
	Name     string `json:"name"`
	Quantity int    `json:"quantity"`
}

// catalog simula o conteúdo que, em produção, viria do banco de dados e
// ficaria no cache em memória.
var catalog = []item{
	{SKU: "GQ-001", Name: "Teclado mecânico", Quantity: 42},
	{SKU: "GQ-002", Name: "Mouse sem fio", Quantity: 17},
	{SKU: "GQ-003", Name: "Monitor 27 polegadas", Quantity: 5},
}

// cache é mantido vivo durante toda a execução do processo.
var cache []byte

func main() {
	log.SetFlags(log.LstdFlags | log.LUTC)
	log.SetPrefix("inventory-api ")
	if len(os.Args) > 1 {
		switch os.Args[1] {
		case "probe":
			os.Exit(probe(os.Args[2:]))
		default:
			fmt.Fprintf(os.Stderr, "uso: %s [probe <url>]\n", os.Args[0])
			os.Exit(2)
		}
	}
	if err := serve(); err != nil {
		log.Printf("erro fatal: %v", err)
		os.Exit(1)
	}
}

// config é tudo o que o serviço precisa receber pelo ambiente.
type config struct {
	databaseURL string
	cacheMB     int
}

func loadConfig() (config, error) {
	cfg := config{cacheMB: defaultCacheMB}
	raw, ok := os.LookupEnv("DATABASE_URL")
	if !ok || raw == "" {
		return cfg, errors.New("a variável de ambiente DATABASE_URL não está definida; " +
			"sem ela o serviço não sabe onde fica o banco de dados (formato esperado: postgres://usuario@host:porta/banco)")
	}
	u, err := url.Parse(raw)
	if err != nil || (u.Scheme != "postgres" && u.Scheme != "postgresql") || u.Host == "" {
		return cfg, fmt.Errorf("DATABASE_URL inválida (%q): formato esperado postgres://usuario@host:porta/banco", raw)
	}
	cfg.databaseURL = raw
	if v, ok := os.LookupEnv("CACHE_MB"); ok && v != "" {
		n, err := strconv.Atoi(v)
		if err != nil || n < 1 || n > 1024 {
			return cfg, fmt.Errorf("CACHE_MB inválido (%q): use um inteiro entre 1 e 1024", v)
		}
		cfg.cacheMB = n
	}
	return cfg, nil
}

// warmCache reserva o cache e escreve em cada página, para que a memória seja
// realmente usada (RSS) e contabilizada pelo cgroup do contêiner.
func warmCache(mb int) {
	cache = make([]byte, mb<<20)
	for i := 0; i < len(cache); i += pageSize {
		cache[i] = byte(i / pageSize)
	}
}

func serve() error {
	cfg, err := loadConfig()
	if err != nil {
		return err
	}
	host := "?"
	if u, err := url.Parse(cfg.databaseURL); err == nil {
		host = u.Host // nunca registrar usuário/senha
	}
	log.Printf("configuração carregada: banco em %s, cache de %d MiB", host, cfg.cacheMB)

	var ready atomic.Bool
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{"status": "ok", "cache_mb": cfg.cacheMB})
	})
	mux.HandleFunc("GET /readyz", func(w http.ResponseWriter, _ *http.Request) {
		if !ready.Load() {
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"status": "aquecendo o cache"})
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"status": "ready"})
	})
	mux.HandleFunc("GET /items", func(w http.ResponseWriter, _ *http.Request) {
		if !ready.Load() {
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"error": "ainda não estou pronto"})
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"items": catalog, "source": "cache"})
	})

	srv := &http.Server{Addr: listenAddr, Handler: logRequests(mux), ReadHeaderTimeout: 5 * time.Second}
	errc := make(chan error, 1)
	go func() { errc <- srv.ListenAndServe() }()
	log.Printf("ouvindo em %s", listenAddr)

	start := time.Now()
	warmCache(cfg.cacheMB)
	ready.Store(true)
	log.Printf("cache de %d MiB aquecido em %s; pronto para receber tráfego", cfg.cacheMB, time.Since(start).Round(time.Millisecond))

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGTERM, syscall.SIGINT)
	select {
	case err := <-errc:
		return err
	case sig := <-stop:
		log.Printf("sinal %s recebido; encerrando", sig)
		ready.Store(false)
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		return srv.Shutdown(ctx)
	}
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

// logRequests registra apenas respostas de erro, para não poluir o log com
// as verificações periódicas do kubelet.
func logRequests(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		rec := &statusRecorder{ResponseWriter: w, status: http.StatusOK}
		next.ServeHTTP(rec, r)
		if rec.status >= 400 {
			log.Printf("%s %s -> %d", r.Method, r.URL.Path, rec.status)
		}
	})
}

type statusRecorder struct {
	http.ResponseWriter
	status int
}

func (r *statusRecorder) WriteHeader(code int) {
	r.status = code
	r.ResponseWriter.WriteHeader(code)
}

func probe(args []string) int {
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "uso: /app probe <url>")
		return 2
	}
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(args[0])
	if err != nil {
		fmt.Printf("ERRO %v\n", err)
		return 1
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(resp.Body, 64<<10))
	fmt.Printf("HTTP %d\n%s", resp.StatusCode, body)
	if resp.StatusCode < 200 || resp.StatusCode > 299 {
		return 1
	}
	return 0
}
