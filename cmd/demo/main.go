// Command demo sobe o binário do serviço de pedidos, dispara pedidos lentos,
// envia SIGTERM no meio (como o Kubernetes faz ao encerrar um Pod) e mostra o
// que aconteceu: respostas, readiness, código de saída e o journal.
//
//	make demo        (dentro do container — ./start.sh já executa uma vez)
package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"
)

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintln(os.Stderr, "uso: demo <caminho do binário orders>")
		os.Exit(2)
	}
	dir, err := os.MkdirTemp("", "orders-demo-")
	if err != nil {
		fail(err)
	}
	defer os.RemoveAll(dir)
	journal := filepath.Join(dir, "orders.journal")

	cmd := exec.Command(os.Args[1])
	cmd.Env = append(os.Environ(), "ADDR=127.0.0.1:0", "JOURNAL_PATH="+journal,
		"WORK_DELAY=800ms", "DRAIN_DELAY=300ms", "SHUTDOWN_TIMEOUT=5s")
	stdout, _ := cmd.StdoutPipe()
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Start(); err != nil {
		fail(err)
	}
	addr := make(chan string, 1)
	go func() {
		sc := bufio.NewScanner(stdout)
		for sc.Scan() {
			var m map[string]any
			if json.Unmarshal(sc.Bytes(), &m) == nil && m["msg"] == "listening" {
				addr <- m["addr"].(string)
			}
		}
	}()
	var base string
	select {
	case a := <-addr:
		base = "http://" + a
	case <-time.After(10 * time.Second):
		_ = cmd.Process.Kill()
		fail(fmt.Errorf("o serviço não subiu em 10s"))
	}

	const n = 10
	fmt.Printf("→ %d pedidos de ~800ms cada; SIGTERM 200ms depois\n", n)
	var mu sync.Mutex
	statuses := map[string]int{}
	var wg sync.WaitGroup
	for i := range n {
		wg.Go(func() {
			body := fmt.Sprintf(`{"customer":"c%d","items":[{"sku":"CAFE","qty":1,"price_cents":3290}]}`, i)
			resp, err := http.Post(base+"/orders", "application/json", strings.NewReader(body))
			key := ""
			if err != nil {
				key = "erro de conexão"
			} else {
				io.Copy(io.Discard, resp.Body)
				resp.Body.Close()
				key = resp.Status
			}
			mu.Lock()
			statuses[key]++
			mu.Unlock()
		})
	}
	time.Sleep(200 * time.Millisecond)
	sigAt := time.Now()
	_ = cmd.Process.Signal(syscall.SIGTERM)
	time.Sleep(50 * time.Millisecond)
	ready := probe(base + "/readyz")

	wg.Wait()
	_ = cmd.Wait()
	took := time.Since(sigAt).Round(10 * time.Millisecond)

	data, _ := os.ReadFile(journal)
	lines, partial := 0, 0
	for _, l := range strings.SplitAfter(string(data), "\n") {
		if l == "" {
			continue
		}
		if !strings.HasSuffix(l, "\n") || !json.Valid([]byte(strings.TrimSpace(l))) {
			partial++
		} else {
			lines++
		}
	}

	fmt.Printf("  respostas dos pedidos em andamento: %v   (esperado: map[201 Created:%d])\n", statuses, n)
	fmt.Printf("  /readyz 50ms após o SIGTERM:        %s   (esperado: 503)\n", ready)
	fmt.Printf("  código de saída:                    %d   (esperado: 0)\n", cmd.ProcessState.ExitCode())
	fmt.Printf("  saiu após o SIGTERM em:             %v   (esperado: ~1.1s — drenagem + trabalho restante)\n", took)
	fmt.Printf("  journal: %d registro(s) íntegro(s), %d parcial(is)   (esperado: %d e 0)\n", lines, partial, n)
	if s := strings.TrimSpace(stderr.String()); s != "" {
		fmt.Printf("  stderr do serviço:\n    %s\n", strings.ReplaceAll(clip(s), "\n", "\n    "))
	}
}

func probe(url string) string {
	c := &http.Client{Timeout: time.Second, Transport: &http.Transport{DisableKeepAlives: true}}
	resp, err := c.Get(url)
	if err != nil {
		return "erro de conexão"
	}
	resp.Body.Close()
	return resp.Status
}

func clip(s string) string {
	lines := strings.Split(s, "\n")
	if len(lines) > 8 {
		lines = append(lines[:8], "…")
	}
	return strings.Join(lines, "\n")
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "demo:", err)
	os.Exit(1)
}
