// Command catalog-api é o serviço de catálogo usado no desafio-19.
//
// Este código NÃO faz parte do que você deve alterar: o desafio está na forma
// como o Deployment troca uma versão pela outra (k8s/). Ele é versionado para
// que você entenda o comportamento de cada versão publicada.
//
// A versão e as funcionalidades são definidas no build (veja app/Dockerfile):
//
//	gitquest/catalog-api:1.0.0  versão estável em produção
//	gitquest/catalog-api:1.0.1  correção pequena, sem mudança de comportamento
//	gitquest/catalog-api:2.0.0  nova funcionalidade: preços no catálogo
//
// Modos de execução:
//
//	/app                      servidor HTTP em :8080
//	/app probe <url>          faz um GET e imprime "HTTP <status>" + corpo; sai com
//	                          código 0 somente para respostas 2xx. A imagem não tem
//	                          shell nem curl: use kubectl exec ... -- /app probe <url>
//	/app loadcheck <url>      faz GETs contínuos (10 por segundo, sem reaproveitar
//	                          conexões) e registra cada falha. Usado pelo avaliador
//	                          para medir a disponibilidade durante um rollout.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	neturl "net/url"
	"os"
	"os/signal"
	"sort"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"
	"time"
)

// Definidos com -ldflags "-X main.version=... -X main.features=...".
var (
	version  = "dev"
	features = ""
)

const (
	listenAddr = ":8080"
	// warmupTime: tempo para carregar o catálogo na memória. Até lá o serviço
	// responde 503 em /readyz e /items.
	warmupTime = 3 * time.Second
	// drainTime: depois do SIGTERM o processo deixa de se anunciar pronto, mas
	// continua atendendo por este tempo, enquanto o Service o tira do balanceamento.
	drainTime = 5 * time.Second
	// priceRefreshEvery: intervalo de recarga da tabela de preços (2.0.0).
	priceRefreshEvery = 6 * time.Second
	// staleGrace: por quanto tempo preços desatualizados ainda são servidos.
	staleGrace = 15 * time.Second
)

type item struct {
	SKU        string `json:"sku"`
	Name       string `json:"name"`
	Quantity   int    `json:"quantity"`
	PriceCents *int   `json:"price_cents,omitempty"`
}

var catalog = []item{
	{SKU: "GQ-101", Name: "Caderno pontilhado", Quantity: 120},
	{SKU: "GQ-102", Name: "Caneta técnica 0.3", Quantity: 80},
	{SKU: "GQ-103", Name: "Régua de alumínio 30 cm", Quantity: 35},
}

// Tabelas de preços da 2.0.0 (formato "SKU;centavos", uma linha por produto):
// bootstrapPrices é carregada na inicialização; priceFeed, nas recargas.
const bootstrapPrices = `GQ-101;2490
GQ-102;1290
GQ-103;3150`

const priceFeed = `GQ-101;2490
GQ-102;12,90
GQ-103;3150`

func hasFeature(name string) bool {
	for _, f := range strings.Split(features, ",") {
		if strings.TrimSpace(f) == name {
			return true
		}
	}
	return false
}

func main() {
	log.SetFlags(log.LstdFlags | log.LUTC)
	log.SetPrefix("catalog-api " + version + " ")
	if len(os.Args) > 1 {
		switch os.Args[1] {
		case "probe":
			os.Exit(probe(os.Args[2:]))
		case "loadcheck":
			os.Exit(loadcheck(os.Args[2:]))
		default:
			fmt.Fprintf(os.Stderr, "uso: %s [probe <url> | loadcheck <url>]\n", os.Args[0])
			os.Exit(2)
		}
	}
	if err := serve(); err != nil {
		log.Printf("erro fatal: %v", err)
		os.Exit(1)
	}
}

// server guarda o estado observado pelos endpoints.
type server struct {
	warm       atomic.Bool
	draining   atomic.Bool
	started    time.Time    // leitura monotônica: durações não dependem do relógio de parede
	staleSince atomic.Int64 // nanossegundos desde started (+1) em que os preços ficaram desatualizados; 0 = em dia
	prices     atomic.Pointer[map[string]int]
}

func (s *server) staleFor() time.Duration {
	since := s.staleSince.Load()
	if since == 0 {
		return 0
	}
	// Monotônico: se o relógio do nó for corrigido para trás (WSL2, NTP), a duração
	// não fica negativa e a readiness não volta a passar sozinha.
	return time.Since(s.started) - time.Duration(since-1)
}

func serve() error {
	s := &server{started: time.Now()}
	pricing := hasFeature("pricing")
	log.Printf("iniciando (funcionalidades: %q)", features)

	mux := http.NewServeMux()
	mux.HandleFunc("GET /version", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{"version": version})
	})
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{"status": "ok", "version": version})
	})
	mux.HandleFunc("GET /readyz", func(w http.ResponseWriter, _ *http.Request) {
		switch {
		case !s.warm.Load():
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"status": "carregando o catálogo"})
		case s.draining.Load():
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"status": "encerrando"})
		case s.staleFor() > 0:
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"status": "tabela de preços desatualizada"})
		default:
			writeJSON(w, http.StatusOK, map[string]any{"status": "ready", "version": version})
		}
	})
	mux.HandleFunc("GET /items", func(w http.ResponseWriter, _ *http.Request) {
		if !s.warm.Load() {
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"error": "catálogo ainda carregando", "version": version})
			return
		}
		if stale := s.staleFor(); stale > staleGrace {
			writeJSON(w, http.StatusInternalServerError, map[string]any{
				"error": fmt.Sprintf("preços expirados há %s", (stale - staleGrace).Round(time.Second)), "version": version})
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"items": s.items(), "version": version})
	})

	srv := &http.Server{Addr: listenAddr, Handler: logRequests(mux), ReadHeaderTimeout: 5 * time.Second}
	errc := make(chan error, 1)
	go func() { errc <- srv.ListenAndServe() }()
	log.Printf("ouvindo em %s", listenAddr)

	go func() {
		time.Sleep(warmupTime)
		if pricing {
			table, err := parsePrices(bootstrapPrices)
			if err != nil {
				errc <- err
				return
			}
			s.prices.Store(&table)
			go s.refreshPrices()
		}
		s.warm.Store(true)
		log.Printf("catálogo carregado em %s; pronto para receber tráfego", warmupTime)
	}()

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGTERM, syscall.SIGINT)
	select {
	case err := <-errc:
		return err
	case sig := <-stop:
		s.draining.Store(true)
		log.Printf("sinal %s recebido; deixando de anunciar prontidão e atendendo por mais %s", sig, drainTime)
		time.Sleep(drainTime)
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		return srv.Shutdown(ctx)
	}
}

func (s *server) items() []item {
	out := make([]item, len(catalog))
	copy(out, catalog)
	if p := s.prices.Load(); p != nil {
		for i := range out {
			if cents, ok := (*p)[out[i].SKU]; ok {
				out[i].PriceCents = &cents
			}
		}
	}
	return out
}

// refreshPrices recarrega a tabela de preços periodicamente (2.0.0). Se a
// recarga falha, o serviço continua servindo os preços antigos por staleGrace
// e avisa, pelo /readyz, que não deve receber tráfego novo.
func (s *server) refreshPrices() {
	for range time.Tick(priceRefreshEvery) {
		table, err := parsePrices(priceFeed)
		if err != nil {
			if s.staleSince.CompareAndSwap(0, int64(time.Since(s.started))+1) {
				log.Printf("ERRO ao recarregar a tabela de preços: %v", err)
				log.Printf("readiness passa a falhar; /items continua respondendo com os preços antigos por mais %s e depois responde 500", staleGrace)
			}
			continue
		}
		s.prices.Store(&table)
		s.staleSince.Store(0)
	}
}

func parsePrices(feed string) (map[string]int, error) {
	table := map[string]int{}
	for n, line := range strings.Split(strings.TrimSpace(feed), "\n") {
		sku, raw, ok := strings.Cut(strings.TrimSpace(line), ";")
		if !ok {
			return nil, fmt.Errorf("linha %d: esperado SKU;centavos, obtido %q", n+1, line)
		}
		cents, err := strconv.Atoi(raw)
		if err != nil {
			return nil, fmt.Errorf("linha %d (%s): preço inválido %q", n+1, sku, raw)
		}
		table[sku] = cents
	}
	return table, nil
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
		if rec.status >= 500 && r.URL.Path != "/readyz" {
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

// loadcheck imprime uma linha "FAIL <hora> <motivo>" por requisição que falhou
// e, a cada 5 s, "STAT ok=<n> fail=<n> versions=<versão>:<n>,...".
func loadcheck(args []string) int {
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "uso: /app loadcheck <url>")
		return 2
	}
	url := args[0]
	client := &http.Client{
		Timeout:   2 * time.Second,
		Transport: &http.Transport{DisableKeepAlives: true},
	}
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGTERM, syscall.SIGINT)

	var ok, fail int
	versions := map[string]int{}
	stat := func() {
		keys := make([]string, 0, len(versions))
		for v := range versions {
			keys = append(keys, v)
		}
		sort.Strings(keys)
		parts := make([]string, len(keys))
		for i, v := range keys {
			parts[i] = fmt.Sprintf("%s:%d", v, versions[v])
		}
		fmt.Printf("STAT ok=%d fail=%d versions=%s\n", ok, fail, strings.Join(parts, ","))
	}
	fmt.Printf("loadcheck: GET %s a cada 100ms\n", url)
	tick := time.NewTicker(100 * time.Millisecond)
	report := time.NewTicker(5 * time.Second)
	defer tick.Stop()
	defer report.Stop()
	for {
		select {
		case <-stop:
			stat()
			return 0
		case <-report.C:
			stat()
		case <-tick.C:
			v, err := fetchVersion(client, url)
			if err != nil {
				fail++
				fmt.Printf("FAIL %s %v\n", time.Now().UTC().Format("15:04:05.000"), err)
				continue
			}
			ok++
			versions[v]++
		}
	}
}

func fetchVersion(client *http.Client, url string) (string, error) {
	resp, err := client.Get(url)
	if err != nil {
		var uerr *neturl.Error
		if errors.As(err, &uerr) {
			return "", uerr.Err // sem repetir o método e a URL em cada linha
		}
		return "", err
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(resp.Body, 64<<10))
	var payload struct {
		Version string `json:"version"`
		Error   string `json:"error"`
	}
	_ = json.Unmarshal(body, &payload)
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("HTTP %d de %s: %s", resp.StatusCode, orUnknown(payload.Version), orUnknown(payload.Error))
	}
	return orUnknown(payload.Version), nil
}

func orUnknown(s string) string {
	if s == "" {
		return "?"
	}
	return s
}
