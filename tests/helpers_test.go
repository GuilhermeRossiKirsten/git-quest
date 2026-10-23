// Package tests contém os testes de contrato da API (avaliador do desafio).
// Todos sobem a API real (src/api) num httptest.Server e conversam com ela por
// HTTP, como os clientes de verdade.
package tests

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"mime"
	"net/http"
	"net/http/httptest"
	"net/url"
	"regexp"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"desafio34/internal/store"
	"desafio34/src/api"
)

// env é uma API rodando sobre um store novo.
type env struct {
	t     *testing.T
	store *store.Store
	srv   *httptest.Server
	hc    *http.Client
}

// newEnv sobe a API com os ganchos dados (pode ser store.Hooks{}).
func newEnv(t *testing.T, hooks store.Hooks) *env {
	t.Helper()
	st := store.NewWithHooks(hooks)
	srv := httptest.NewUnstartedServer(api.New(st))
	// Pânicos de handler viram "sem resposta HTTP" no teste; o stack trace do
	// net/http só poluiria o relatório.
	srv.Config.ErrorLog = log.New(io.Discard, "", 0)
	srv.Start()
	t.Cleanup(srv.Close)
	hc := srv.Client()
	hc.Timeout = 10 * time.Second
	return &env{t: t, store: st, srv: srv, hc: hc}
}

// seed grava produtos direto no store (Seq crescente na ordem dada).
func (e *env) seed(prices ...int64) []store.Product {
	out := make([]store.Product, 0, len(prices))
	for i, c := range prices {
		out = append(out, e.store.CreateProduct(store.Product{
			Name: fmt.Sprintf("Produto %02d", i+1), PriceCents: c, Stock: 50,
		}))
	}
	return out
}

// resp é uma resposta HTTP já lida.
type resp struct {
	Method, Path string
	Status       int
	Header       http.Header
	Body         []byte
}

func (r resp) String() string {
	body := strings.TrimSpace(string(r.Body))
	if len(body) > 300 {
		body = body[:300] + "…"
	}
	ct := r.Header.Get("Content-Type")
	return fmt.Sprintf("%s %s → %d (Content-Type %q) %s", r.Method, r.Path, r.Status, ct, body)
}

// do envia uma requisição. body pode ser nil, string (enviada crua) ou
// qualquer valor (serializado em JSON). hdr são pares nome, valor.
func (e *env) do(method, path string, body any, hdr ...string) resp {
	e.t.Helper()
	var rd io.Reader
	switch b := body.(type) {
	case nil:
	case string:
		rd = strings.NewReader(b)
	default:
		raw, err := json.Marshal(b)
		if err != nil {
			e.t.Fatal(err)
		}
		rd = bytes.NewReader(raw)
	}
	req, err := http.NewRequest(method, e.srv.URL+path, rd)
	if err != nil {
		e.t.Fatal(err)
	}
	if rd != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	for i := 0; i+1 < len(hdr); i += 2 {
		req.Header.Set(hdr[i], hdr[i+1])
	}
	res, err := e.hc.Do(req)
	if err != nil {
		// Status 0: o teste que chamou reporta a falha com este corpo.
		return resp{Method: method, Path: path, Header: http.Header{},
			Body: []byte("sem resposta HTTP (o handler entrou em pânico?): " + err.Error())}
	}
	defer res.Body.Close()
	data, err := io.ReadAll(res.Body)
	if err != nil {
		e.t.Fatalf("%s %s: lendo a resposta: %v", method, path, err)
	}
	return resp{Method: method, Path: path, Status: res.StatusCode, Header: res.Header, Body: data}
}

// object decodifica o corpo como objeto JSON (falha o teste se não for).
func object(t *testing.T, r resp) map[string]any {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal(r.Body, &m); err != nil {
		t.Fatalf("o corpo não é um objeto JSON: %v\n        %s", err, r)
	}
	return m
}

// mediaType devolve o tipo de mídia do Content-Type, sem parâmetros.
func mediaType(r resp) string {
	mt, _, err := mime.ParseMediaType(r.Header.Get("Content-Type"))
	if err != nil {
		return ""
	}
	return mt
}

// Pedaços de implementação que não devem vazar para o cliente (Error Report).
var leaks = []string{"store:", "json:", "strconv.", "runtime error", "goroutine "}

// problemErr verifica se r é um problem+json (RFC 9457) com um dos status
// esperados. Devolve "" se estiver tudo certo, ou a explicação do que falta.
func problemErr(r resp, want ...int) string {
	var errs []string
	okStatus := false
	for _, w := range want {
		okStatus = okStatus || r.Status == w
	}
	if !okStatus {
		errs = append(errs, fmt.Sprintf("status %d, esperado %v", r.Status, want))
	}
	var p map[string]any
	if mt := mediaType(r); mt != "application/problem+json" {
		errs = append(errs, fmt.Sprintf("Content-Type %q, esperado application/problem+json", r.Header.Get("Content-Type")))
	} else if err := json.Unmarshal(r.Body, &p); err != nil {
		errs = append(errs, "corpo não é um objeto JSON")
	} else {
		typ, _ := p["type"].(string)
		if _, err := url.Parse(typ); typ == "" || err != nil {
			errs = append(errs, `"type" ausente ou não é uma URI`)
		}
		if s, _ := p["title"].(string); strings.TrimSpace(s) == "" {
			errs = append(errs, `"title" ausente`)
		}
		if s, _ := p["detail"].(string); strings.TrimSpace(s) == "" {
			errs = append(errs, `"detail" ausente`)
		} else {
			for _, l := range leaks {
				if strings.Contains(s, l) {
					errs = append(errs, fmt.Sprintf(`"detail" vaza detalhe interno (%q)`, l))
				}
			}
		}
		if st, ok := p["status"].(float64); !ok || int(st) != r.Status {
			errs = append(errs, fmt.Sprintf(`"status" no corpo (%v) diferente do status HTTP %d`, p["status"], r.Status))
		}
	}
	if len(errs) == 0 {
		return ""
	}
	return strings.Join(errs, "; ") + "\n        " + r.String()
}

var etagSyntax = regexp.MustCompile(`^(W/)?"[\x21\x23-\x7e\x80-\xff]*"$`)

// strongETag devolve o ETag da resposta, exigindo um ETag forte bem formado.
func strongETag(t *testing.T, r resp) string {
	t.Helper()
	tag := r.Header.Get("ETag")
	switch {
	case tag == "":
		t.Fatalf("resposta sem cabeçalho ETag\n        %s", r)
	case !etagSyntax.MatchString(tag):
		t.Fatalf("ETag %q mal formado: precisa ser uma string entre aspas, ex.: \"abc123\" (RFC 9110, 8.8.3)\n        %s", tag, r)
	case strings.HasPrefix(tag, "W/"):
		t.Fatalf("ETag %q é fraco (W/): para If-Match o servidor precisa de validadores fortes\n        %s", tag, r)
	}
	return tag
}

// gate pausa a primeira escrita armada de uma operação do store até open.
// Com ele os testes intercalam duas requisições de forma determinística.
type gate struct {
	op       store.Op
	armed    atomic.Bool
	entered  chan struct{}
	release  chan struct{}
	openOnce sync.Once
}

func newGate(t *testing.T, op store.Op) *gate {
	g := &gate{op: op, entered: make(chan struct{}), release: make(chan struct{})}
	t.Cleanup(g.open)
	return g
}

// hook é o store.Hooks.BeforeWrite.
func (g *gate) hook(op store.Op, _ string) {
	if op == g.op && g.armed.CompareAndSwap(true, false) {
		close(g.entered)
		<-g.release
	}
}

func (g *gate) arm()  { g.armed.Store(true) }
func (g *gate) open() { g.openOnce.Do(func() { close(g.release) }) }

// interleave executa a até ela pausar no gate (dentro da escrita no store),
// então executa b; espera b terminar (por até 1,5 s — se b ficar esperando a,
// tudo bem), libera a e devolve as duas respostas. bEarly diz se b terminou
// antes de a ser liberada.
func interleave(t *testing.T, g *gate, a, b func() resp) (ra, rb resp, bEarly bool) {
	t.Helper()
	chA, chB := make(chan resp, 1), make(chan resp, 1)
	g.arm()
	go func() { chA <- a() }()
	select {
	case <-g.entered:
	case ra = <-chA:
		t.Fatalf("a primeira requisição terminou sem chegar a gravar no store (%s) — esperava-se que ela gravasse", ra)
	case <-time.After(5 * time.Second):
		t.Fatal("a primeira requisição não chegou ao store em 5 s")
	}
	go func() { chB <- b() }()
	select {
	case rb = <-chB:
		bEarly = true
	case <-time.After(1500 * time.Millisecond):
	}
	g.open()
	select {
	case ra = <-chA:
	case <-time.After(5 * time.Second):
		t.Fatal("a primeira requisição não terminou 5 s depois de liberada")
	}
	if !bEarly {
		select {
		case rb = <-chB:
		case <-time.After(5 * time.Second):
			t.Fatal("a segunda requisição não terminou 5 s depois de a primeira ser liberada")
		}
	}
	return ra, rb, bEarly
}

// sendAsync é e.do para goroutines: erros viram resposta com Status 0.
func (e *env) sendAsync(method, path string, body any, hdr ...string) resp {
	raw, _ := json.Marshal(body)
	req, _ := http.NewRequest(method, e.srv.URL+path, bytes.NewReader(raw))
	req.Header.Set("Content-Type", "application/json")
	for i := 0; i+1 < len(hdr); i += 2 {
		req.Header.Set(hdr[i], hdr[i+1])
	}
	res, err := e.hc.Do(req)
	if err != nil {
		return resp{Method: method, Path: path, Body: []byte("erro de transporte: " + err.Error()), Header: http.Header{}}
	}
	defer res.Body.Close()
	data, _ := io.ReadAll(res.Body)
	return resp{Method: method, Path: path, Status: res.StatusCode, Header: res.Header, Body: data}
}
