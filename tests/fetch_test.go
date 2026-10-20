package tests

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"desafio8/src/fetch"
)

// Folgas de tempo: o contrato pede retorno em < 100ms após o cancelamento; os
// testes aceitam bem mais para não falharem por lentidão do -race ou do host.
const (
	cancelSlack = 400 * time.Millisecond
	holdFor     = 3 * time.Second // quanto um handler "lento" segura a resposta
)

func quickPolicy(attempts int) fetch.RetryPolicy {
	return fetch.RetryPolicy{
		MaxAttempts: attempts,
		BaseDelay:   10 * time.Millisecond,
		MaxDelay:    80 * time.Millisecond,
	}
}

// cancelAfter chama cancel depois de d e registra o instante do cancelamento.
func cancelAfter(d time.Duration, cancel context.CancelFunc) *atomic.Pointer[time.Time] {
	var at atomic.Pointer[time.Time]
	time.AfterFunc(d, func() {
		now := time.Now()
		at.Store(&now)
		cancel()
	})
	return &at
}

func equalDelays(a, b []time.Duration) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// ---------------------------------------------------------------------------
// Comportamento básico
// ---------------------------------------------------------------------------

func TestBasico_Sucesso(t *testing.T) {
	srv := newServer(t, func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "tabela de preços v42")
	})
	c, tr := newClient(t)

	body, err := fetch.Get(context.Background(), c, srv.URL, quickPolicy(3))
	if err != nil {
		t.Fatalf("Get retornou erro inesperado: %v", err)
	}
	if string(body) != "tabela de preços v42" {
		t.Errorf("corpo = %q; esperado %q", body, "tabela de preços v42")
	}
	if n := srv.hits.Load(); n != 1 {
		t.Errorf("servidor recebeu %d requisições; esperado 1", n)
	}
	assertBodiesClosed(t, tr)
}

func TestConcorrencia_ChamadasSimultaneas(t *testing.T) {
	srv := newServer(t, func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, r.URL.Query().Get("id"))
	})
	c, tr := newClient(t)

	var wg sync.WaitGroup
	errs := make(chan error, 32)
	for i := range 32 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			want := strconv.Itoa(i)
			body, err := fetch.Get(context.Background(), c, srv.URL+"?id="+want, quickPolicy(2))
			if err != nil {
				errs <- err
			} else if string(body) != want {
				errs <- fmt.Errorf("corpo = %q; esperado %q", body, want)
			}
		}()
	}
	wg.Wait()
	close(errs)
	for err := range errs {
		t.Error(err)
	}
	assertBodiesClosed(t, tr)
}

// ---------------------------------------------------------------------------
// Política de retry
// ---------------------------------------------------------------------------

func TestRetry_ErrosTransitorios(t *testing.T) {
	cases := []struct {
		name string
		fail http.HandlerFunc
	}{
		{"503", status(http.StatusServiceUnavailable)},
		{"500", status(http.StatusInternalServerError)},
		{"502", status(http.StatusBadGateway)},
		{"conexão derrubada", hangUp},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			srv := newServer(t, failThen(2, tc.fail, "ok"))
			c, _ := newClient(t)
			rec := &sleepRecorder{}
			p := quickPolicy(3)
			p.Sleep = rec.Sleep

			body, err := fetch.Get(context.Background(), c, srv.URL, p)
			if err != nil {
				t.Fatalf("esperava sucesso na 3ª tentativa; erro: %v", err)
			}
			if string(body) != "ok" {
				t.Errorf("corpo = %q; esperado %q", body, "ok")
			}
			if n := srv.hits.Load(); n != 3 {
				t.Errorf("servidor recebeu %d requisições; esperado 3", n)
			}
			want := []time.Duration{10 * time.Millisecond, 20 * time.Millisecond}
			if got := rec.Delays(); !equalDelays(got, want) {
				t.Errorf("esperas entre tentativas = %v; esperado %v", got, want)
			}
		})
	}

	t.Run("esgota MaxAttempts", func(t *testing.T) {
		srv := newServer(t, status(http.StatusServiceUnavailable))
		c, _ := newClient(t)
		p := quickPolicy(4)
		p.Sleep = (&sleepRecorder{}).Sleep
		if _, err := fetch.Get(context.Background(), c, srv.URL, p); err == nil {
			t.Fatal("esperava erro após esgotar as tentativas")
		}
		if n := srv.hits.Load(); n != 4 {
			t.Errorf("servidor recebeu %d requisições; esperado 4 (MaxAttempts)", n)
		}
	})

	t.Run("MaxAttempts zero faz uma tentativa", func(t *testing.T) {
		srv := newServer(t, func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, "ok") })
		c, _ := newClient(t)
		body, err := fetch.Get(context.Background(), c, srv.URL, fetch.RetryPolicy{})
		if err != nil || string(body) != "ok" {
			t.Errorf("Get com RetryPolicy{} = (%q, %v); esperado (\"ok\", nil)", body, err)
		}
		if n := srv.hits.Load(); n != 1 {
			t.Errorf("servidor recebeu %d requisições; esperado 1", n)
		}
	})
}

func TestRetry_NaoRepete4xx(t *testing.T) {
	for _, code := range []int{400, 401, 403, 404, 410} {
		t.Run(strconv.Itoa(code), func(t *testing.T) {
			srv := newServer(t, status(code))
			c, _ := newClient(t)
			p := quickPolicy(5)
			rec := &sleepRecorder{}
			p.Sleep = rec.Sleep

			_, err := fetch.Get(context.Background(), c, srv.URL, p)
			if err == nil {
				t.Fatal("esperava erro")
			}
			if n := srv.hits.Load(); n != 1 {
				t.Errorf("status %d: servidor recebeu %d requisições; erros 4xx não devem ser repetidos", code, n)
			}
			if d := rec.Delays(); len(d) != 0 {
				t.Errorf("status %d: houve espera(s) %v; não deveria haver nova tentativa", code, d)
			}
			var se *fetch.StatusError
			if !errors.As(err, &se) || se.StatusCode != code {
				t.Errorf("errors.As(err, *StatusError) deveria encontrar status %d; err = %v", code, err)
			}
		})
	}
}

func TestRetry_429RespeitaRetryAfter(t *testing.T) {
	tooMany := func(retryAfter string) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			if retryAfter != "" {
				w.Header().Set("Retry-After", retryAfter)
			}
			http.Error(w, "devagar", http.StatusTooManyRequests)
		}
	}
	cases := []struct {
		name, header string
		want         time.Duration
	}{
		{"Retry-After 2s", "2", 2 * time.Second},
		{"sem Retry-After usa backoff", "", 10 * time.Millisecond},
		{"Retry-After inválido usa backoff", "amanhã", 10 * time.Millisecond},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			srv := newServer(t, failThen(1, tooMany(tc.header), "ok"))
			c, _ := newClient(t)
			rec := &sleepRecorder{}
			p := quickPolicy(3)
			p.Sleep = rec.Sleep

			body, err := fetch.Get(context.Background(), c, srv.URL, p)
			if err != nil || string(body) != "ok" {
				t.Fatalf("Get = (%q, %v); esperado (\"ok\", nil) após um 429", body, err)
			}
			if got := rec.Delays(); !equalDelays(got, []time.Duration{tc.want}) {
				t.Errorf("esperas = %v; esperado [%v]", got, tc.want)
			}
		})
	}
}

func TestBackoff_TetoSemOverflow(t *testing.T) {
	t.Run("sequência curta", func(t *testing.T) {
		srv := newServer(t, status(http.StatusServiceUnavailable))
		c, _ := newClient(t)
		rec := &sleepRecorder{}
		p := fetch.RetryPolicy{MaxAttempts: 6, BaseDelay: 10 * time.Millisecond, MaxDelay: 50 * time.Millisecond, Sleep: rec.Sleep}
		fetch.Get(context.Background(), c, srv.URL, p)
		ms := time.Millisecond
		want := []time.Duration{10 * ms, 20 * ms, 40 * ms, 50 * ms, 50 * ms}
		if got := rec.Delays(); !equalDelays(got, want) {
			t.Errorf("esperas = %v; esperado %v", got, want)
		}
	})

	t.Run("100 tentativas", func(t *testing.T) {
		srv := newServer(t, status(http.StatusServiceUnavailable))
		c, _ := newClient(t)
		rec := &sleepRecorder{}
		p := fetch.RetryPolicy{MaxAttempts: 100, BaseDelay: time.Second, MaxDelay: 30 * time.Second, Sleep: rec.Sleep}
		fetch.Get(context.Background(), c, srv.URL, p)
		got := rec.Delays()
		if len(got) != 99 {
			t.Fatalf("%d esperas registradas; esperado 99 (entre 100 tentativas)", len(got))
		}
		for i, d := range got {
			if d <= 0 || d > p.MaxDelay {
				t.Fatalf("espera #%d = %v; deve estar em (0, %v] — houve overflow ou faltou o teto", i+1, d, p.MaxDelay)
			}
			if i > 0 && d < got[i-1] {
				t.Fatalf("espera #%d = %v é menor que a anterior (%v)", i+1, d, got[i-1])
			}
		}
		if got[len(got)-1] != p.MaxDelay {
			t.Errorf("última espera = %v; esperado o teto %v", got[len(got)-1], p.MaxDelay)
		}
	})
}

// ---------------------------------------------------------------------------
// Cancelamento e prazos
// ---------------------------------------------------------------------------

func TestCancelamento_DuranteRequisicao(t *testing.T) {
	cancelled := make(chan struct{}, 1)
	srv := newServer(t, func(w http.ResponseWriter, r *http.Request) {
		hold(r, holdFor, cancelled)
		io.WriteString(w, "tarde demais")
	})
	c, tr := newClient(t)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	cancelledAt := cancelAfter(100*time.Millisecond, cancel)

	_, err := fetch.Get(ctx, c, srv.URL, quickPolicy(1))
	returned := time.Now()
	at := cancelledAt.Load()
	if at == nil {
		t.Fatalf("Get retornou antes do cancelamento (err = %v)", err)
	}
	if lag := returned.Sub(*at); lag > cancelSlack {
		t.Errorf("Get levou %v para retornar após o cancelamento; esperado < %v", lag.Round(time.Millisecond), cancelSlack)
	}
	if !errors.Is(err, context.Canceled) {
		t.Errorf("errors.Is(err, context.Canceled) = false; err = %v", err)
	}
	select {
	case <-cancelled:
	case <-time.After(time.Second):
		t.Error("o servidor não percebeu o cancelamento: a requisição continuou em andamento (ela precisa carregar o contexto)")
	}
	assertNoLeakedGoroutines(t)
	assertBodiesClosed(t, tr)
}

func TestCancelamento_DuranteLeituraDoCorpo(t *testing.T) {
	cancelled := make(chan struct{}, 1)
	srv := newServer(t, func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Length", "1000000")
		w.WriteHeader(http.StatusOK)
		io.WriteString(w, strings.Repeat("x", 1024))
		http.NewResponseController(w).Flush()
		hold(r, holdFor, cancelled)
	})
	c, tr := newClient(t)
	ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel()
	deadline, _ := ctx.Deadline()

	_, err := fetch.Get(ctx, c, srv.URL, quickPolicy(1))
	if lag := time.Since(deadline); lag > cancelSlack {
		t.Errorf("Get levou %v além do prazo para retornar; esperado < %v", lag.Round(time.Millisecond), cancelSlack)
	}
	if !errors.Is(err, context.DeadlineExceeded) {
		t.Errorf("errors.Is(err, context.DeadlineExceeded) = false; err = %v", err)
	}
	assertNoLeakedGoroutines(t)
	assertBodiesClosed(t, tr)
}

func TestCancelamento_DuranteEspera(t *testing.T) {
	srv := newServer(t, status(http.StatusServiceUnavailable))
	c, tr := newClient(t)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	cancelledAt := cancelAfter(150*time.Millisecond, cancel)

	// Sem p.Sleep: usa a espera padrão do pacote.
	p := fetch.RetryPolicy{MaxAttempts: 2, BaseDelay: holdFor, MaxDelay: holdFor}
	_, err := fetch.Get(ctx, c, srv.URL, p)
	returned := time.Now()
	at := cancelledAt.Load()
	if at == nil {
		t.Fatalf("Get retornou antes do cancelamento (err = %v)", err)
	}
	if lag := returned.Sub(*at); lag > cancelSlack {
		t.Errorf("Get levou %v para retornar após o cancelamento durante a espera; esperado < %v",
			lag.Round(time.Millisecond), cancelSlack)
	}
	if !errors.Is(err, context.Canceled) {
		t.Errorf("errors.Is(err, context.Canceled) = false; err = %v", err)
	}
	if n := srv.hits.Load(); n != 1 {
		t.Errorf("servidor recebeu %d requisições; esperado 1 (cancelado durante a primeira espera)", n)
	}
	assertNoLeakedGoroutines(t)
	assertBodiesClosed(t, tr)
}

func TestPrazo_NaoIniciaTentativaInutil(t *testing.T) {
	t.Run("prazo menor que a próxima espera", func(t *testing.T) {
		srv := newServer(t, status(http.StatusServiceUnavailable))
		c, _ := newClient(t)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		rec := &sleepRecorder{}
		p := fetch.RetryPolicy{MaxAttempts: 3, BaseDelay: 5 * time.Second, MaxDelay: 5 * time.Second, Sleep: rec.Sleep}

		start := time.Now()
		_, err := fetch.Get(ctx, c, srv.URL, p)
		if el := time.Since(start); el > 500*time.Millisecond {
			t.Errorf("Get levou %v; deveria desistir logo, pois a espera de 5s não cabe no prazo de 1s", el.Round(time.Millisecond))
		}
		if d := rec.Delays(); len(d) != 0 {
			t.Errorf("Get chamou Sleep(%v) mesmo sem prazo para a próxima tentativa", d)
		}
		if n := srv.hits.Load(); n != 1 {
			t.Errorf("servidor recebeu %d requisições; esperado 1", n)
		}
		if !errors.Is(err, context.DeadlineExceeded) {
			t.Errorf("errors.Is(err, context.DeadlineExceeded) = false; err = %v", err)
		}
		var se *fetch.StatusError
		if !errors.As(err, &se) || se.StatusCode != http.StatusServiceUnavailable {
			t.Errorf("o erro deveria manter a última falha (StatusError 503) na cadeia; err = %v", err)
		}
	})

	t.Run("contexto já cancelado", func(t *testing.T) {
		srv := newServer(t, func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, "ok") })
		c, _ := newClient(t)
		ctx, cancel := context.WithCancel(context.Background())
		cancel()
		_, err := fetch.Get(ctx, c, srv.URL, quickPolicy(3))
		if !errors.Is(err, context.Canceled) {
			t.Errorf("errors.Is(err, context.Canceled) = false; err = %v", err)
		}
		if n := srv.hits.Load(); n != 0 {
			t.Errorf("servidor recebeu %d requisições com o contexto já cancelado; esperado 0", n)
		}
	})
}

// ---------------------------------------------------------------------------
// Recursos e erros
// ---------------------------------------------------------------------------

func TestRecursos_CorposSempreFechados(t *testing.T) {
	cases := []struct {
		name     string
		h        http.HandlerFunc
		attempts int
	}{
		{"200", func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, "ok") }, 1},
		{"404", status(http.StatusNotFound), 1},
		{"503 até esgotar", status(http.StatusServiceUnavailable), 3},
		{"429 e depois 200", failThen(1, status(http.StatusTooManyRequests), "ok"), 3},
		{"500 e depois 200", failThen(2, status(http.StatusInternalServerError), "ok"), 3},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			srv := newServer(t, tc.h)
			c, tr := newClient(t)
			p := quickPolicy(tc.attempts)
			p.Sleep = (&sleepRecorder{}).Sleep
			fetch.Get(context.Background(), c, srv.URL, p)
			if tr.responses.Load() == 0 {
				t.Fatal("nenhuma resposta passou pelo cliente informado (Get deve usar o *http.Client recebido)")
			}
			assertBodiesClosed(t, tr)
		})
	}
}

func TestLimite_CorpoGrande(t *testing.T) {
	const mib = 1 << 20
	stream := func(n int, withLength bool) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			if withLength {
				w.Header().Set("Content-Length", strconv.Itoa(n))
			}
			chunk := strings.Repeat("a", 32<<10)
			for sent := 0; sent < n; sent += len(chunk) {
				if _, err := io.WriteString(w, chunk[:min(len(chunk), n-sent)]); err != nil {
					return // cliente desistiu
				}
			}
		}
	}
	cases := []struct {
		name       string
		size       int
		withLength bool
		maxBytes   int64
		tooLarge   bool
	}{
		{"32 MiB sem Content-Length", 32 * mib, false, mib, true},
		{"32 MiB com Content-Length", 32 * mib, true, mib, true},
		{"exatamente MaxBytes", mib, false, mib, false},
		{"limite padrão de 10 MiB", 11 * mib, false, 0, true},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			srv := newServer(t, stream(tc.size, tc.withLength))
			c, tr := newClient(t)
			p := quickPolicy(3)
			p.MaxBytes = tc.maxBytes
			p.Sleep = (&sleepRecorder{}).Sleep

			body, err := fetch.Get(context.Background(), c, srv.URL, p)
			if tc.tooLarge {
				if !errors.Is(err, fetch.ErrTooLarge) {
					t.Errorf("errors.Is(err, ErrTooLarge) = false; err = %v, len(body) = %d", err, len(body))
				}
				if n := srv.hits.Load(); n != 1 {
					t.Errorf("servidor recebeu %d requisições; ErrTooLarge não deve ser repetido", n)
				}
			} else if err != nil || len(body) != tc.size {
				t.Errorf("Get = (%d bytes, %v); esperado (%d bytes, nil)", len(body), err, tc.size)
			}
			assertBodiesClosed(t, tr)
		})
	}
}

func TestErros_PreservaCausa(t *testing.T) {
	srv := newServer(t, status(http.StatusBadGateway))
	c, _ := newClient(t)
	p := quickPolicy(2)
	p.Sleep = (&sleepRecorder{}).Sleep

	_, err := fetch.Get(context.Background(), c, srv.URL, p)
	if err == nil {
		t.Fatal("esperava erro")
	}
	var se *fetch.StatusError
	if !errors.As(err, &se) {
		t.Fatalf("errors.As(err, *StatusError) = false: a causa original se perdeu; err = %v", err)
	}
	if se.StatusCode != http.StatusBadGateway {
		t.Errorf("StatusCode = %d; esperado 502", se.StatusCode)
	}
	if !strings.Contains(err.Error(), srv.URL) {
		t.Errorf("a mensagem de erro deveria citar a URL; err = %v", err)
	}
}
