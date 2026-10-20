package tests

// Testes de integração: compilam cmd/orders (com -race), sobem o binário numa
// porta livre e enviam SIGTERM de verdade — como o kubelet faz ao encerrar um
// Pod. Precisam de um sistema tipo Unix (o avaliador roda no container Linux).

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"testing"
	"time"

	"desafio17/src/orders"
)

var (
	buildOnce sync.Once
	binPath   string
	buildErr  error
)

// ordersBinary compila cmd/orders uma única vez por execução da suíte.
func ordersBinary(t *testing.T) string {
	t.Helper()
	buildOnce.Do(func() {
		dir, err := os.MkdirTemp("", "orders-bin-")
		if err != nil {
			buildErr = err
			return
		}
		binPath = filepath.Join(dir, "orders")
		out, err := exec.Command("go", "build", "-race", "-o", binPath, "desafio17/cmd/orders").CombinedOutput()
		if err != nil {
			buildErr = fmt.Errorf("go build ./cmd/orders falhou: %v\n%s", err, out)
		}
	})
	if buildErr != nil {
		t.Fatal(buildErr)
	}
	return binPath
}

// proc é uma instância do serviço em execução.
type proc struct {
	t       *testing.T
	cmd     *exec.Cmd
	addr    string
	journal string
	logs    *syncBuffer // stdout (JSON) + stderr
	exited  chan struct{}
	code    int
	exitAt  time.Time
}

func startOrders(t *testing.T, env map[string]string) *proc {
	t.Helper()
	bin := ordersBinary(t)
	p := &proc{t: t, journal: filepath.Join(t.TempDir(), "orders.journal"), logs: &syncBuffer{}, exited: make(chan struct{})}
	p.cmd = exec.Command(bin)
	p.cmd.Env = append(os.Environ(), "ADDR=127.0.0.1:0", "JOURNAL_PATH="+p.journal, "FLUSH_INTERVAL=50ms")
	for k, v := range env {
		p.cmd.Env = append(p.cmd.Env, k+"="+v)
	}
	stdout, err := p.cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	p.cmd.Stderr = p.logs
	if err := p.cmd.Start(); err != nil {
		t.Fatal(err)
	}

	addrCh := make(chan string, 1)
	scanDone := make(chan struct{})
	go func() {
		defer close(scanDone)
		sc := bufio.NewScanner(stdout)
		sc.Buffer(make([]byte, 1<<20), 1<<20)
		for sc.Scan() {
			line := sc.Text()
			p.logs.Write([]byte(line + "\n"))
			var m map[string]any
			if json.Unmarshal([]byte(line), &m) == nil && m["msg"] == "listening" {
				if a, ok := m["addr"].(string); ok {
					select {
					case addrCh <- a:
					default:
					}
				}
			}
		}
	}()
	go func() {
		<-scanDone // Wait só depois de consumir toda a saída
		_ = p.cmd.Wait()
		p.exitAt = time.Now()
		p.code = p.cmd.ProcessState.ExitCode()
		close(p.exited)
	}()
	t.Cleanup(func() {
		select {
		case <-p.exited:
		default:
			_ = p.cmd.Process.Kill()
			<-p.exited
		}
		if t.Failed() {
			t.Logf("logs do serviço:\n%s", clipLines(p.logs.String(), 40))
		}
	})

	select {
	case p.addr = <-addrCh:
	case <-p.exited:
		t.Fatalf("o serviço terminou (código %d) antes de registrar msg=listening", p.code)
	case <-time.After(10 * time.Second):
		t.Fatalf("o serviço não registrou msg=listening em 10s")
	}
	return p
}

func (p *proc) url(path string) string { return "http://" + p.addr + path }

func (p *proc) sigterm() time.Time {
	p.t.Helper()
	if err := p.cmd.Process.Signal(syscall.SIGTERM); err != nil {
		p.t.Fatalf("enviando SIGTERM: %v", err)
	}
	return time.Now()
}

// waitExit espera o processo terminar; devolve false se não terminar em max.
func (p *proc) waitExit(max time.Duration) bool {
	select {
	case <-p.exited:
		return true
	case <-time.After(max):
		return false
	}
}

// freshGet faz um GET numa conexão nova (sem keep-alive), como uma sonda do kubelet.
func freshGet(url string) (int, error) {
	c := &http.Client{Timeout: time.Second, Transport: &http.Transport{DisableKeepAlives: true}}
	resp, err := c.Get(url)
	if err != nil {
		return 0, err
	}
	io.Copy(io.Discard, resp.Body)
	resp.Body.Close()
	return resp.StatusCode, nil
}

type postResult struct {
	status  int
	created orders.Created
	err     error
}

func postOrder(client *http.Client, url, body string) postResult {
	resp, err := client.Post(url, "application/json", strings.NewReader(body))
	if err != nil {
		return postResult{err: err}
	}
	defer resp.Body.Close()
	r := postResult{status: resp.StatusCode}
	b, _ := io.ReadAll(resp.Body)
	_ = json.Unmarshal(b, &r.created)
	return r
}

func orderBody(i int) string {
	return fmt.Sprintf(`{"customer":"cliente-%02d","items":[{"sku":"CAFE-500G","qty":2,"price_cents":3290},`+
		`{"sku":"FILTRO-103","qty":1,"price_cents":1190},{"sku":"CANECA-AZUL","qty":%d,"price_cents":2500}]}`, i, i%5+1)
}

func TestBinario_DesligamentoGracioso(t *testing.T) {
	const (
		n          = 30
		workDelay  = 1200 * time.Millisecond
		drainDelay = 400 * time.Millisecond
	)
	p := startOrders(t, map[string]string{
		"WORK_DELAY": workDelay.String(), "DRAIN_DELAY": drainDelay.String(),
		"SHUTDOWN_TIMEOUT": "5s", "REQUEST_TIMEOUT": "10s",
	})
	if code, err := freshGet(p.url("/readyz")); err != nil || code != 200 {
		t.Fatalf("antes do SIGTERM /readyz deveria responder 200, veio %d %v", code, err)
	}

	client := &http.Client{Timeout: 15 * time.Second}
	results := make([]postResult, n)
	var wg sync.WaitGroup
	for i := range n {
		wg.Go(func() { results[i] = postOrder(client, p.url("/orders"), orderBody(i)) })
	}
	time.Sleep(250 * time.Millisecond) // todos os pedidos já estão em andamento
	sigAt := p.sigterm()

	// Durante a drenagem: readiness 503 (sai do balanceador), liveness 200.
	time.Sleep(50 * time.Millisecond)
	ready, readyErr := freshGet(p.url("/readyz"))
	live, liveErr := freshGet(p.url("/healthz"))
	if readyErr != nil || ready != http.StatusServiceUnavailable {
		t.Errorf("logo após o SIGTERM /readyz deveria responder 503 (para o balanceador parar de mandar tráfego); veio %d %v", ready, readyErr)
	}
	if liveErr != nil || live != http.StatusOK {
		t.Errorf("durante a drenagem /healthz deveria continuar 200 (o processo está vivo; reiniciá-lo agora perderia pedidos); veio %d %v", live, liveErr)
	}

	// Depois da drenagem: novas conexões recusadas, pedidos antigos ainda em andamento.
	time.Sleep(time.Until(sigAt.Add(drainDelay + 250*time.Millisecond)))
	if conn, err := net.DialTimeout("tcp", p.addr, 300*time.Millisecond); err == nil {
		conn.Close()
		t.Errorf("%v após o SIGTERM (drenagem de %v já encerrada) o serviço ainda aceita conexões novas", time.Since(sigAt).Round(time.Millisecond), drainDelay)
	}

	wg.Wait()
	if !p.waitExit(10 * time.Second) {
		t.Fatalf("o processo não terminou em 10s após concluir os pedidos")
	}
	took := p.exitAt.Sub(sigAt)

	ok := map[string]bool{}
	var failures []string
	for i, r := range results {
		switch {
		case r.err != nil:
			failures = append(failures, fmt.Sprintf("pedido %d: %v", i, r.err))
		case r.status != http.StatusCreated:
			failures = append(failures, fmt.Sprintf("pedido %d: status %d", i, r.status))
		default:
			ok[r.created.OrderID] = true
		}
	}
	if len(failures) > 0 {
		t.Errorf("%d de %d pedidos em andamento no SIGTERM não receberam 201; ex.: %s", len(failures), n, failures[0])
	}
	if p.code != 0 {
		t.Errorf("desligamento limpo deveria sair com código 0, saiu com %d", p.code)
	}
	if took < workDelay-250*time.Millisecond {
		t.Errorf("o processo saiu %v após o SIGTERM, antes de os pedidos em andamento (trabalho de %v) terminarem", took.Round(time.Millisecond), workDelay)
	}
	if limit := drainDelay + workDelay + 2*time.Second; took > limit {
		t.Errorf("o processo levou %v para sair após o SIGTERM (limite %v)", took.Round(time.Millisecond), limit)
	}
	if strings.Contains(p.logs.String(), "DATA RACE") {
		t.Errorf("o race detector encontrou data race no serviço (veja os logs abaixo)")
	}

	rep := readJournal(t, p.journal)
	if len(rep.broken) > 0 {
		t.Errorf("journal com %d registro(s) parcial(is)/corrompido(s), ex.: %q", len(rep.broken), rep.broken[0])
	}
	inJournal := map[string]bool{}
	for _, r := range rep.records {
		inJournal[r.OrderID] = true
	}
	if len(rep.records) != n || len(inJournal) != n {
		t.Errorf("journal tem %d registro(s) íntegro(s) (%d distintos); esperava exatamente %d", len(rep.records), len(inJournal), n)
	}
	for id := range ok {
		if !inJournal[id] {
			t.Errorf("pedido %s foi confirmado ao cliente (201) mas não está no journal", id)
			break
		}
	}
}

func TestBinario_PrazoEstourado(t *testing.T) {
	const shutdownTimeout = time.Second
	p := startOrders(t, map[string]string{
		"WORK_DELAY": "30s", "DRAIN_DELAY": "100ms",
		"SHUTDOWN_TIMEOUT": shutdownTimeout.String(), "REQUEST_TIMEOUT": "60s",
	})
	res := make(chan postResult, 1)
	go func() {
		res <- postOrder(&http.Client{Timeout: 20 * time.Second}, p.url("/orders"), orderBody(1))
	}()
	time.Sleep(250 * time.Millisecond)
	sigAt := p.sigterm()

	if !p.waitExit(shutdownTimeout + 5*time.Second) {
		t.Fatalf("com uma requisição travada, o processo não saiu em %v após o SIGTERM: o prazo de desligamento não é respeitado", shutdownTimeout+5*time.Second)
	}
	took := p.exitAt.Sub(sigAt)
	if took < shutdownTimeout {
		t.Errorf("o processo saiu %v após o SIGTERM, antes do prazo de %v: não esperou a requisição em andamento", took.Round(time.Millisecond), shutdownTimeout)
	}
	if limit := 100*time.Millisecond + shutdownTimeout + 1500*time.Millisecond; took > limit {
		t.Errorf("o processo levou %v para sair (prazo %v + drenagem 100ms; limite %v)", took.Round(time.Millisecond), shutdownTimeout, limit)
	}
	if p.code == 0 || p.code == -1 {
		t.Errorf("prazo de desligamento estourado deveria sair com código ≠ 0 (e não morto por sinal); código = %d", p.code)
	}
	select {
	case r := <-res:
		if r.err == nil && r.status == http.StatusCreated {
			t.Errorf("a requisição travada não poderia ter sido confirmada (201)")
		}
	case <-time.After(3 * time.Second):
		t.Errorf("a conexão da requisição travada não foi encerrada após o fechamento forçado")
	}
	if rep := readJournal(t, p.journal); len(rep.records) != 0 || len(rep.broken) != 0 {
		t.Errorf("o pedido abandonado não pode estar no journal: %d registro(s), %d parcial(is)", len(rep.records), len(rep.broken))
	}
}

func TestBinario_PanicoNaoDerruba(t *testing.T) {
	p := startOrders(t, map[string]string{"WORK_DELAY": "10ms", "DRAIN_DELAY": "100ms"})
	client := &http.Client{Timeout: 5 * time.Second}

	// Um pedido sem itens faz o código legado dividir por zero.
	r := postOrder(client, p.url("/orders"), `{"customer":"zeca","items":[]}`)
	if r.err != nil {
		t.Errorf("pedido sem itens: a conexão caiu (%v) — esperava uma resposta HTTP (400 ou 500)", r.err)
	} else if r.status != http.StatusBadRequest && r.status != http.StatusInternalServerError {
		t.Errorf("pedido sem itens: status %d, esperava 400 ou 500", r.status)
	}
	if code, err := freshGet(p.url("/healthz")); err != nil || code != 200 {
		t.Fatalf("depois do pedido sem itens o serviço deveria continuar vivo; /healthz: %d %v", code, err)
	}
	if r := postOrder(client, p.url("/orders"), orderBody(7)); r.err != nil || r.status != http.StatusCreated {
		t.Errorf("pedido normal depois do pânico: %d %v, esperava 201", r.status, r.err)
	}
	p.sigterm()
	if !p.waitExit(5 * time.Second) {
		t.Fatalf("o processo não saiu em 5s após o SIGTERM")
	}
	if p.code != 0 {
		t.Errorf("desligamento limpo deveria sair com código 0, saiu com %d", p.code)
	}
}
