package tests

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"desafio17/src/httpx"
	"desafio17/src/orders"
)

func TestJournal_CloseGravaTudo(t *testing.T) {
	path := filepath.Join(t.TempDir(), "orders.journal")
	j, err := orders.OpenJournal(path)
	if err != nil {
		t.Fatal(err)
	}
	for i := 1; i <= 3; i++ {
		if err := j.Append(sampleRecord(fmt.Sprintf("ord-%d", i))); err != nil {
			t.Fatalf("Append: %v", err)
		}
	}
	if err := j.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	rep := readJournal(t, path)
	if len(rep.records) != 3 || len(rep.broken) != 0 {
		t.Errorf("depois de Close o arquivo deveria ter os 3 registros aceitos; tem %d íntegro(s) e %d quebrado(s) %v",
			len(rep.records), len(rep.broken), rep.broken)
	}
	if err := j.Append(sampleRecord("ord-depois")); !errors.Is(err, orders.ErrClosed) {
		t.Errorf("Append depois de Close: err = %v, esperava ErrClosed", err)
	}
	if err := j.Close(); err != nil {
		t.Errorf("segundo Close deveria ser inofensivo, veio %v", err)
	}
}

func TestJournal_ConcorrenciaIntegra(t *testing.T) {
	path := filepath.Join(t.TempDir(), "orders.journal")
	j, err := orders.OpenJournal(path)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	runDone := make(chan struct{})
	go func() {
		defer close(runDone)
		j.Run(ctx, time.Millisecond) // descarga agressiva, concorrente com Append
	}()

	const writers, each = 8, 60
	var wg sync.WaitGroup
	for w := 0; w < writers; w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < each; i++ {
				if err := j.Append(sampleRecord(fmt.Sprintf("ord-%d-%d", w, i))); err != nil {
					t.Errorf("Append: %v", err)
					return
				}
				if i%10 == 0 {
					time.Sleep(time.Millisecond)
				}
			}
		}()
	}
	wg.Wait()
	cancel()
	select {
	case <-runDone:
	case <-time.After(2 * time.Second):
		t.Fatalf("Run não retornou 2s depois do cancelamento do contexto")
	}
	if err := j.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}

	rep := readJournal(t, path)
	seen := map[string]bool{}
	for _, r := range rep.records {
		seen[r.OrderID] = true
	}
	if len(rep.broken) > 0 {
		t.Errorf("%d linha(s) corrompida(s)/intercalada(s) no journal, ex.: %q", len(rep.broken), rep.broken[0])
	}
	if len(rep.records) != writers*each || len(seen) != writers*each {
		t.Errorf("journal tem %d registro(s) íntegro(s) (%d distintos); esperava %d", len(rep.records), len(seen), writers*each)
	}
}

func TestJournal_WorkerParaComContexto(t *testing.T) {
	check := leakCheck(t, ordersPkg)
	path := filepath.Join(t.TempDir(), "orders.journal")
	j, err := orders.OpenJournal(path)
	if err != nil {
		t.Fatal(err)
	}
	defer j.Close()

	ctx, cancel := context.WithCancel(context.Background())
	runDone := make(chan struct{})
	go func() {
		defer close(runDone)
		j.Run(ctx, time.Hour) // o ticker nunca dispara durante o teste
	}()

	select {
	case <-runDone:
		t.Errorf("Run retornou antes do cancelamento: o worker deve bloquear até ctx terminar (quem chama decide a goroutine)")
	case <-time.After(100 * time.Millisecond):
	}
	if err := j.Append(sampleRecord("ord-1")); err != nil {
		t.Fatalf("Append: %v", err)
	}
	cancel()
	select {
	case <-runDone:
	case <-time.After(time.Second):
		t.Fatalf("Run não retornou 1s depois do cancelamento do contexto")
	}
	if rep := readJournal(t, path); len(rep.records) != 1 {
		t.Errorf("ao parar, o worker deveria descarregar o buffer: o arquivo tem %d registro(s) íntegro(s), esperava 1", len(rep.records))
	}
	check("depois que Run retornou")
}

func TestPedidos_ExpiradoNaoGrava(t *testing.T) {
	path := filepath.Join(t.TempDir(), "orders.journal")
	j, err := orders.OpenJournal(path)
	if err != nil {
		t.Fatal(err)
	}
	logger, _ := newLogger()
	svc := &orders.Service{Journal: j, WorkDelay: 300 * time.Millisecond, Logger: logger}

	post := func(timeout time.Duration, body string) (*http.Response, string) {
		t.Helper()
		srv := httptest.NewServer(httpx.RequestID(httpx.Timeout(timeout, http.HandlerFunc(svc.CreateOrder))))
		defer srv.Close()
		resp, err := http.Post(srv.URL, "application/json", strings.NewReader(body))
		if err != nil {
			t.Fatalf("POST: %v", err)
		}
		b, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		return resp, string(b)
	}
	order := `{"customer":"ana","items":[{"sku":"CAFE-500G","qty":2,"price_cents":3290}]}`

	// 1) Pedido normal: 201, gravado com o mesmo order_id e request_id da resposta.
	resp, body := post(2*time.Second, order)
	var created orders.Created
	_ = json.Unmarshal([]byte(body), &created)
	if resp.StatusCode != http.StatusCreated || created.OrderID == "" {
		t.Fatalf("pedido normal: resposta %d %q, esperava 201 com order_id", resp.StatusCode, body)
	}
	if created.RequestID == "" || created.RequestID != resp.Header.Get(httpx.HeaderRequestID) {
		t.Errorf("pedido normal: request_id do corpo (%q) deveria ser o do cabeçalho X-Request-ID (%q)",
			created.RequestID, resp.Header.Get(httpx.HeaderRequestID))
	}

	// 2) Pedido cujo prazo estoura no meio do trabalho: o cliente recebe 503,
	//    então o pedido NÃO pode aparecer no journal depois.
	resp, body = post(80*time.Millisecond, order)
	if resp.StatusCode != http.StatusServiceUnavailable {
		t.Errorf("pedido com prazo de 80ms e trabalho de 300ms: resposta %d %q, esperava 503", resp.StatusCode, body)
	}
	time.Sleep(500 * time.Millisecond) // tempo de sobra para um handler teimoso terminar
	if err := j.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}

	rep := readJournal(t, path)
	if len(rep.records) != 1 {
		t.Errorf("journal tem %d registro(s); esperava só o pedido normal — o cliente que recebeu 503 acredita que o pedido não existe", len(rep.records))
	}
	if len(rep.records) >= 1 {
		r := rep.records[0]
		if r.OrderID != created.OrderID || r.RequestID != created.RequestID || r.TotalCents != 6580 {
			t.Errorf("registro gravado %+v não corresponde à resposta %+v (total esperado 6580)", r, created)
		}
	}
}
