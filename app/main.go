// Command warmup-api é o serviço de recomendações usado no desafio-18.
//
// Este código NÃO faz parte do que você deve alterar: o problema está nos
// manifests em k8s/. Ele é versionado para que você entenda exatamente como o
// processo se comporta ao iniciar, quando a dependência cai e quando trava.
//
// Modos de execução (a mesma imagem serve aos dois Deployments):
//
//	/app               warmup-api em :8080
//	/app pricing       a dependência "pricing" (simulada) em :8080
//	/app probe <url>   faz um GET e imprime "HTTP <status>" + corpo; sai com
//	                   código 0 somente para respostas 2xx. A imagem não tem
//	                   shell nem curl, então use: kubectl exec ... -- /app probe <url>
//
// Ciclo de vida do warmup-api:
//
//  1. Ao iniciar, carrega o modelo de recomendação na memória. Isso leva
//     WARMUP_SECONDS segundos (padrão 25) e, durante esse tempo, a porta 8080
//     AINDA NÃO ESTÁ ABERTA: nenhuma requisição é atendida.
//  2. Com o modelo carregado, abre a porta 8080 e passa a consultar a
//     dependência pricing (PRICING_URL) a cada 2 s.
//  3. Todas as requisições passam pelo loop principal do serviço. Se esse loop
//     travar (simulado por GET /admin/hang), o processo continua vivo, mas
//     nenhuma requisição volta a ser respondida — só um reinício resolve.
//
// Endpoints do warmup-api:
//
//	GET /healthz        200 enquanto o loop principal responde (não olha a dependência)
//	GET /readyz         200 se o pricing está disponível; 503 caso contrário
//	GET /work           uma recomendação (precisa do pricing); 503 sem ele
//	GET /admin/hang     simula um deadlock no loop principal (irreversível)
//
// Endpoints do pricing:
//
//	GET /healthz                       200 sempre
//	GET /status                        200 se o estado é "up", 503 se "down"
//	GET /admin/state?state=up|down     muda o estado (simula a queda da dependência)
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"sync/atomic"
	"syscall"
	"time"
)

const (
	listenAddr           = ":8080"
	defaultWarmupSeconds = 25
	defaultPricingURL    = "http://pricing"
	dependencyInterval   = 2 * time.Second
	dependencyTimeout    = 1 * time.Second
)

func main() {
	log.SetFlags(log.LstdFlags | log.LUTC)
	mode := ""
	if len(os.Args) > 1 {
		mode = os.Args[1]
	}
	var err error
	switch mode {
	case "":
		log.SetPrefix("warmup-api ")
		err = serveWarmupAPI()
	case "pricing":
		log.SetPrefix("pricing ")
		err = servePricing()
	case "probe":
		os.Exit(probe(os.Args[2:]))
	default:
		fmt.Fprintf(os.Stderr, "uso: %s [pricing | probe <url>]\n", os.Args[0])
		os.Exit(2)
	}
	if err != nil {
		log.Printf("erro fatal: %v", err)
		os.Exit(1)
	}
}

// ---------------------------------------------------------------------------
// warmup-api

func warmupSeconds() (int, error) {
	v, ok := os.LookupEnv("WARMUP_SECONDS")
	if !ok || v == "" {
		return defaultWarmupSeconds, nil
	}
	n, err := strconv.Atoi(v)
	if err != nil || n < 0 || n > 600 {
		return 0, fmt.Errorf("WARMUP_SECONDS inválido (%q): use um inteiro entre 0 e 600", v)
	}
	return n, nil
}

// mainLoop serializa o trabalho do serviço. Cada requisição pede uma "vez" ao
// loop; se o loop travar, as requisições ficam esperando para sempre.
type mainLoop struct {
	turns chan chan struct{}
	hang  chan struct{}
}

func newMainLoop() *mainLoop {
	l := &mainLoop{turns: make(chan chan struct{}), hang: make(chan struct{}, 1)}
	go l.run()
	return l
}

func (l *mainLoop) run() {
	for {
		select {
		case done := <-l.turns:
			close(done)
		case <-l.hang:
			log.Printf("loop principal travado (deadlock simulado): nenhuma requisição será respondida até o processo reiniciar")
			select {} // bloqueia para sempre
		}
	}
}

// turn espera a vez no loop principal; devolve false se o cliente desistiu antes.
func (l *mainLoop) turn(ctx context.Context) bool {
	done := make(chan struct{})
	select {
	case l.turns <- done:
	case <-ctx.Done():
		return false
	}
	select {
	case <-done:
		return true
	case <-ctx.Done():
		return false
	}
}

func serveWarmupAPI() error {
	warmup, err := warmupSeconds()
	if err != nil {
		return err
	}
	pricingURL := os.Getenv("PRICING_URL")
	if pricingURL == "" {
		pricingURL = defaultPricingURL
	}
	hostname, _ := os.Hostname()
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGTERM, syscall.SIGINT)

	// 1. Carregamento do modelo: a porta 8080 só abre no final.
	start := time.Now()
	log.Printf("carregando o modelo de recomendação (WARMUP_SECONDS=%d); a porta 8080 abre quando terminar", warmup)
	deadline := start.Add(time.Duration(warmup) * time.Second)
	for time.Now().Before(deadline) {
		select {
		case sig := <-stop:
			log.Printf("sinal %s recebido durante o carregamento (%.0fs de %ds); encerrando", sig, time.Since(start).Seconds(), warmup)
			return nil
		case <-time.After(min(5*time.Second, time.Until(deadline))):
			if remaining := time.Until(deadline); remaining > 0 {
				log.Printf("carregando o modelo... %.0fs de %ds", time.Since(start).Seconds(), warmup)
			}
		}
	}
	log.Printf("modelo carregado em %s", time.Since(start).Round(time.Second))

	// 2. Dependência pricing, consultada em segundo plano.
	var dependencyUp atomic.Bool
	checkDependency := func() {
		ctx, cancel := context.WithTimeout(context.Background(), dependencyTimeout)
		defer cancel()
		up := false
		req, _ := http.NewRequestWithContext(ctx, http.MethodGet, pricingURL+"/status", nil)
		if resp, err := http.DefaultClient.Do(req); err == nil {
			_, _ = io.Copy(io.Discard, resp.Body)
			resp.Body.Close()
			up = resp.StatusCode == http.StatusOK
		}
		if old := dependencyUp.Swap(up); old != up {
			if up {
				log.Printf("dependência pricing disponível (%s)", pricingURL)
			} else {
				log.Printf("dependência pricing INDISPONÍVEL (%s): /readyz e /work respondem 503 até ela voltar", pricingURL)
			}
		}
	}
	dependencyUp.Store(true)
	checkDependency()
	go func() {
		for range time.Tick(dependencyInterval) {
			checkDependency()
		}
	}()

	// 3. HTTP, sempre através do loop principal.
	loop := newMainLoop()
	startedAt := time.Now()
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		if !loop.turn(r.Context()) {
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{
			"status": "ok", "role": "warmup-api", "pod": hostname,
			"warmup_seconds": warmup, "uptime_seconds": int(time.Since(startedAt).Seconds()),
		})
	})
	mux.HandleFunc("GET /readyz", func(w http.ResponseWriter, r *http.Request) {
		if !loop.turn(r.Context()) {
			return
		}
		if !dependencyUp.Load() {
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"status": "dependência pricing indisponível"})
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"status": "ready"})
	})
	mux.HandleFunc("GET /work", func(w http.ResponseWriter, r *http.Request) {
		if !loop.turn(r.Context()) {
			return
		}
		if !dependencyUp.Load() {
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"error": "sem preços: dependência pricing indisponível"})
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"recommendation": "GQ-042", "pod": hostname})
	})
	mux.HandleFunc("GET /admin/hang", func(w http.ResponseWriter, _ *http.Request) {
		select {
		case loop.hang <- struct{}{}:
		default:
		}
		writeJSON(w, http.StatusAccepted, map[string]any{"status": "travando o loop principal", "pod": hostname})
	})

	srv := &http.Server{Addr: listenAddr, Handler: logRequests(mux), ReadHeaderTimeout: 5 * time.Second}
	errc := make(chan error, 1)
	go func() { errc <- srv.ListenAndServe() }()
	log.Printf("ouvindo em %s; dependência em %s", listenAddr, pricingURL)
	return waitAndShutdown(srv, errc, stop)
}

// ---------------------------------------------------------------------------
// pricing (dependência simulada)

func servePricing() error {
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGTERM, syscall.SIGINT)
	var down atomic.Bool
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{"status": "ok", "role": "pricing"})
	})
	mux.HandleFunc("GET /status", func(w http.ResponseWriter, _ *http.Request) {
		if down.Load() {
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"status": "down"})
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"status": "up"})
	})
	mux.HandleFunc("GET /admin/state", func(w http.ResponseWriter, r *http.Request) {
		switch state := r.URL.Query().Get("state"); state {
		case "up", "down":
			down.Store(state == "down")
			log.Printf("estado alterado para %q", state)
			writeJSON(w, http.StatusOK, map[string]any{"status": state})
		default:
			writeJSON(w, http.StatusBadRequest, map[string]any{"error": "use ?state=up ou ?state=down"})
		}
	})
	srv := &http.Server{Addr: listenAddr, Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	errc := make(chan error, 1)
	go func() { errc <- srv.ListenAndServe() }()
	log.Printf("ouvindo em %s", listenAddr)
	return waitAndShutdown(srv, errc, stop)
}

// ---------------------------------------------------------------------------
// utilitários

// waitAndShutdown encerra rápido ao receber SIGTERM: requisições presas no
// loop travado não seguram o processo por mais de 2 s.
func waitAndShutdown(srv *http.Server, errc <-chan error, stop <-chan os.Signal) error {
	select {
	case err := <-errc:
		return err
	case sig := <-stop:
		log.Printf("sinal %s recebido; encerrando", sig)
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		if err := srv.Shutdown(ctx); err != nil {
			log.Printf("requisições pendentes abandonadas: %v", err)
		}
		return nil
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
