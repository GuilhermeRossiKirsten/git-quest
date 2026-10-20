package tests

import (
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"strings"
	"testing"
	"time"

	"desafio17/src/httpx"
)

func TestRequestID_PropagaEGera(t *testing.T) {
	var seen []string
	h := httpx.RequestID(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		seen = append(seen, httpx.RequestIDFrom(r.Context()))
		io.WriteString(w, "ok")
	}))
	srv := httptest.NewServer(h)
	defer srv.Close()

	get := func(incoming string) (header, inHandler string) {
		t.Helper()
		req, _ := http.NewRequest("GET", srv.URL, nil)
		if incoming != "" {
			req.Header.Set(httpx.HeaderRequestID, incoming)
		}
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatalf("GET: %v", err)
		}
		io.Copy(io.Discard, resp.Body)
		resp.Body.Close()
		return resp.Header.Get(httpx.HeaderRequestID), seen[len(seen)-1]
	}

	h1, in1 := get("")
	if h1 == "" {
		t.Errorf("sem X-Request-ID recebido: a resposta deveria trazer o cabeçalho %s com o id gerado (veio vazio)", httpx.HeaderRequestID)
	}
	if in1 == "" {
		t.Errorf("o handler não encontrou o id no contexto (RequestIDFrom vazio)")
	}
	if h1 != "" && in1 != h1 {
		t.Errorf("id no contexto (%q) difere do cabeçalho da resposta (%q)", in1, h1)
	}
	if _, in2 := get(""); in2 == in1 {
		t.Errorf("duas requisições receberam o mesmo id gerado %q", in1)
	}
	if h, in := get("pedido-abc_123.x"); h != "pedido-abc_123.x" || in != "pedido-abc_123.x" {
		t.Errorf("X-Request-ID válido recebido deveria ser preservado: cabeçalho=%q contexto=%q", h, in)
	}
	for _, bad := range []string{strings.Repeat("a", 65), "tem espaço", "quebra\tlinha"} {
		if _, in := get(bad); in == bad || in == "" {
			t.Errorf("X-Request-ID inválido %q deveria ser substituído por um id gerado (contexto=%q)", bad, in)
		}
	}
}

func TestLogging_RegistraStatus(t *testing.T) {
	logger, buf := newLogger()
	mux := http.NewServeMux()
	mux.HandleFunc("/so-corpo", func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, "oi") })
	mux.HandleFunc("/nao-existe", func(w http.ResponseWriter, r *http.Request) { http.NotFound(w, r) })
	h := httpx.RequestID(httpx.Logging(logger, mux))

	for _, path := range []string{"/so-corpo", "/nao-existe"} {
		req := httptest.NewRequest("GET", path, nil)
		req.Header.Set(httpx.HeaderRequestID, "rid-"+strings.TrimPrefix(path, "/"))
		h.ServeHTTP(httptest.NewRecorder(), req)
	}

	entries := logEntries(t, buf.String())
	if len(entries) != 2 {
		t.Fatalf("esperava 2 linhas de log (uma por requisição), veio %d:\n%s", len(entries), buf.String())
	}
	want := []struct {
		path   string
		status float64
	}{{"/so-corpo", 200}, {"/nao-existe", 404}}
	for i, e := range entries {
		if e["msg"] != "request" || e["method"] != "GET" || e["path"] != want[i].path {
			t.Errorf("linha %d: esperava msg=request method=GET path=%s, veio %v", i+1, want[i].path, e)
		}
		if e["status"] != want[i].status {
			t.Errorf("%s: status registrado = %v, esperava %v (o handler que só chama Write responde 200 implicitamente)",
				want[i].path, e["status"], want[i].status)
		}
		if _, ok := e["duration_ms"].(float64); !ok {
			t.Errorf("%s: campo duration_ms ausente ou não numérico: %v", want[i].path, e)
		}
		if e["request_id"] != "rid-"+strings.TrimPrefix(want[i].path, "/") {
			t.Errorf("%s: request_id registrado = %v", want[i].path, e["request_id"])
		}
	}
}

func TestRecover_PanicoVira500(t *testing.T) {
	logger, buf := newLogger()
	h := httpx.Recover(logger, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var m map[string]int
		m["boom"]++ // pânico: escrita em mapa nil
	}))
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest("POST", "/orders", nil))
	if rec.Code != http.StatusInternalServerError {
		t.Errorf("pânico no handler: status = %d, esperava 500", rec.Code)
	}
	if !strings.Contains(buf.String(), `"msg":"panic"`) || !strings.Contains(buf.String(), "/orders") {
		t.Errorf("o pânico deveria ser registrado no log (msg=panic, com o caminho); log:\n%s", buf.String())
	}

	ok := httpx.Recover(logger, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusAccepted)
	}))
	rec = httptest.NewRecorder()
	ok.ServeHTTP(rec, httptest.NewRequest("GET", "/", nil))
	if rec.Code != http.StatusAccepted {
		t.Errorf("sem pânico, a resposta do handler deveria passar intacta: status = %d", rec.Code)
	}

	t.Run("ErrAbortHandler", func(t *testing.T) {
		abort := httpx.Recover(logger, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			panic(http.ErrAbortHandler)
		}))
		defer func() {
			if p := recover(); p != http.ErrAbortHandler {
				t.Errorf("panic(http.ErrAbortHandler) é o jeito oficial de abortar a resposta e deve ser repassado ao net/http; recuperado: %v", p)
			}
		}()
		abort.ServeHTTP(httptest.NewRecorder(), httptest.NewRequest("GET", "/", nil))
	})
}

func TestTimeout_RespondeNoPrazo(t *testing.T) {
	sawDeadline := make(chan bool, 1)
	slow := httpx.Timeout(100*time.Millisecond, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, ok := r.Context().Deadline()
		sawDeadline <- ok
		<-r.Context().Done()
	}))
	srv := httptest.NewServer(slow)
	defer srv.Close()

	start := time.Now()
	resp, err := http.Get(srv.URL)
	if err != nil {
		t.Fatalf("GET: %v", err)
	}
	body, _ := io.ReadAll(resp.Body)
	resp.Body.Close()
	el := time.Since(start)
	if resp.StatusCode != http.StatusServiceUnavailable || strings.TrimSpace(string(body)) != httpx.TimeoutBody {
		t.Errorf("handler lento: resposta %d %q, esperava 503 %s", resp.StatusCode, body, httpx.TimeoutBody)
	}
	if el < 90*time.Millisecond || el > 600*time.Millisecond {
		t.Errorf("handler lento: resposta em %v, esperava ~100ms", el)
	}
	if !<-sawDeadline {
		t.Errorf("o contexto entregue ao handler deveria ter prazo (Deadline)")
	}

	fast := httpx.Timeout(time.Second, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("X-Teste", "sim")
		w.WriteHeader(http.StatusCreated)
		io.WriteString(w, "criado")
	}))
	rec := httptest.NewRecorder()
	fast.ServeHTTP(rec, httptest.NewRequest("POST", "/", nil))
	if rec.Code != http.StatusCreated || rec.Body.String() != "criado" || rec.Header().Get("X-Teste") != "sim" {
		t.Errorf("handler rápido: resposta %d %q (X-Teste=%q), esperava 201 \"criado\" com X-Teste=sim",
			rec.Code, rec.Body.String(), rec.Header().Get("X-Teste"))
	}
}

func TestTimeout_SemVazamentoNemCorrida(t *testing.T) {
	check := leakCheck(t, httpxPkg)
	// Handler que ignora o prazo e escreve na resposta depois que ele estourou.
	late := httpx.Timeout(50*time.Millisecond, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(150 * time.Millisecond)
		w.Header().Set("X-Tarde", "sim")
		io.WriteString(w, "tarde demais")
	}))
	for i := 0; i < 3; i++ {
		rec := httptest.NewRecorder()
		late.ServeHTTP(rec, httptest.NewRequest("GET", "/", nil))
		time.Sleep(250 * time.Millisecond) // o handler termina sua escrita tardia
		if rec.Code != http.StatusServiceUnavailable {
			t.Errorf("status = %d, esperava 503", rec.Code)
		}
		if strings.Contains(rec.Body.String(), "tarde") || rec.Header().Get("X-Tarde") != "" {
			t.Errorf("a escrita do handler depois do prazo vazou para a resposta: %q", rec.Body.String())
		}
	}
	check("depois que os handlers terminaram")
}

// TestTimeout_PanicoPropagado roda o cenário num processo filho: se o pânico
// escapar de Recover, ele derruba o processo inteiro — exatamente o sintoma
// em produção — e o processo pai relata isso em vez de morrer junto.
func TestTimeout_PanicoPropagado(t *testing.T) {
	if os.Getenv("GQ_CHILD_PANIC") == "1" {
		logger, _ := newLogger()
		h := httpx.Recover(logger, httpx.Timeout(time.Second, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			var items []int
			_ = items[3] // pânico: índice fora do intervalo
		})))
		rec := httptest.NewRecorder()
		h.ServeHTTP(rec, httptest.NewRequest("POST", "/orders", nil))
		os.Stdout.WriteString("GQ_STATUS=" + http.StatusText(rec.Code) + "\n")
		return
	}
	cmd := exec.Command(os.Args[0], "-test.run=^TestTimeout_PanicoPropagado$", "-test.count=1")
	cmd.Env = append(os.Environ(), "GQ_CHILD_PANIC=1")
	out, err := cmd.CombinedOutput()
	switch {
	case strings.Contains(string(out), "GQ_STATUS="+http.StatusText(500)):
		// ok
	case strings.Contains(string(out), "GQ_STATUS="):
		t.Errorf("pânico dentro de Timeout, com Recover por fora: esperava 500, veio %s", grab(string(out), "GQ_STATUS="))
	default:
		t.Errorf("pânico dentro de Timeout derrubou o processo inteiro (Recover não o capturou; err=%v):\n%s", err, clipLines(string(out), 12))
	}
}

func grab(s, prefix string) string {
	i := strings.Index(s, prefix)
	rest := s[i+len(prefix):]
	if j := strings.IndexByte(rest, '\n'); j >= 0 {
		rest = rest[:j]
	}
	return rest
}

func clipLines(s string, n int) string {
	lines := strings.Split(s, "\n")
	if len(lines) > n {
		lines = append(lines[:n], "…")
	}
	return strings.Join(lines, "\n")
}
