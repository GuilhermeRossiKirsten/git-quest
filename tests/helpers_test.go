package tests

import (
	"bytes"
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"runtime"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

// fetchPkg identifica frames do pacote avaliado em dumps de goroutines.
const fetchPkg = "desafio8/src/fetch."

// server é um httptest.Server que conta as requisições recebidas.
type server struct {
	*httptest.Server
	hits atomic.Int64
}

func newServer(t *testing.T, h http.HandlerFunc) *server {
	t.Helper()
	s := &server{}
	s.Server = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		s.hits.Add(1)
		h(w, r)
	}))
	t.Cleanup(func() {
		s.CloseClientConnections()
		s.Close()
	})
	return s
}

// trackingTransport embrulha cada corpo de resposta para saber se foi fechado.
type trackingTransport struct {
	base      *http.Transport
	responses atomic.Int64
	open      atomic.Int64
}

func (t *trackingTransport) RoundTrip(req *http.Request) (*http.Response, error) {
	resp, err := t.base.RoundTrip(req)
	if resp != nil && resp.Body != nil {
		t.responses.Add(1)
		t.open.Add(1)
		resp.Body = &trackedBody{ReadCloser: resp.Body, onClose: func() { t.open.Add(-1) }}
	}
	return resp, err
}

type trackedBody struct {
	io.ReadCloser
	once    sync.Once
	onClose func()
}

func (b *trackedBody) Close() error {
	b.once.Do(b.onClose)
	return b.ReadCloser.Close()
}

// newClient cria um cliente com transporte próprio e instrumentado.
func newClient(t *testing.T) (*http.Client, *trackingTransport) {
	t.Helper()
	tr := &trackingTransport{base: &http.Transport{}}
	t.Cleanup(tr.base.CloseIdleConnections)
	return &http.Client{Transport: tr}, tr
}

func assertBodiesClosed(t *testing.T, tr *trackingTransport) {
	t.Helper()
	if n := tr.open.Load(); n != 0 {
		t.Errorf("%d de %d corpo(s) de resposta não foram fechados após Get retornar (conexões vazando)",
			n, tr.responses.Load())
	}
}

// sleepRecorder substitui a espera entre tentativas e registra cada duração.
type sleepRecorder struct {
	mu     sync.Mutex
	delays []time.Duration
}

func (r *sleepRecorder) Sleep(ctx context.Context, d time.Duration) error {
	r.mu.Lock()
	r.delays = append(r.delays, d)
	r.mu.Unlock()
	return ctx.Err()
}

func (r *sleepRecorder) Delays() []time.Duration {
	r.mu.Lock()
	defer r.mu.Unlock()
	return append([]time.Duration(nil), r.delays...)
}

// status responde sempre com o código informado.
func status(code int) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, http.StatusText(code), code)
	}
}

// failThen falha n vezes com o handler fail e depois responde body com 200.
func failThen(n int64, fail http.HandlerFunc, body string) http.HandlerFunc {
	var calls atomic.Int64
	return func(w http.ResponseWriter, r *http.Request) {
		if calls.Add(1) <= n {
			fail(w, r)
			return
		}
		io.WriteString(w, body)
	}
}

// hangUp derruba a conexão sem responder (erro de rede do ponto de vista do cliente).
func hangUp(w http.ResponseWriter, r *http.Request) {
	conn, _, err := http.NewResponseController(w).Hijack()
	if err != nil {
		panic(err)
	}
	conn.Close()
}

// assertNoLeakedGoroutines falha se, após um curto período, ainda houver
// goroutines executando código do pacote fetch (ex.: uma requisição largada
// em segundo plano depois que Get retornou).
func assertNoLeakedGoroutines(t *testing.T) {
	t.Helper()
	deadline := time.Now().Add(500 * time.Millisecond)
	buf := make([]byte, 1<<20)
	for {
		n := runtime.Stack(buf, true)
		if !bytes.Contains(buf[:n], []byte(fetchPkg)) {
			return
		}
		if time.Now().After(deadline) {
			t.Errorf("goroutine(s) do pacote fetch continuam vivas depois que Get retornou:\n%s",
				leakedStacks(buf[:n]))
			return
		}
		time.Sleep(20 * time.Millisecond)
	}
}

func leakedStacks(dump []byte) []byte {
	var out [][]byte
	for _, g := range bytes.Split(dump, []byte("\n\n")) {
		if bytes.Contains(g, []byte(fetchPkg)) {
			out = append(out, g)
		}
	}
	return bytes.Join(out, []byte("\n\n"))
}

// hold bloqueia o handler até o cliente desistir (r.Context) ou até max.
// Informa em cancelled se o servidor percebeu o cancelamento.
func hold(r *http.Request, max time.Duration, cancelled chan<- struct{}) {
	select {
	case <-r.Context().Done():
		select {
		case cancelled <- struct{}{}:
		default:
		}
	case <-time.After(max):
	}
}
