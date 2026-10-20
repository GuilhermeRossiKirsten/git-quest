package tests

import (
	"bufio"
	"bytes"
	"encoding/json"
	"errors"
	"log/slog"
	"os"
	"regexp"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"

	"desafio17/src/orders"
)

// Prefixos dos pacotes avaliados, para reconhecer suas goroutines em dumps.
const (
	httpxPkg  = "desafio17/src/httpx."
	ordersPkg = "desafio17/src/orders."
)

var goroutineHeader = regexp.MustCompile(`^goroutine (\d+) `)

// goroutinesWith devolve id -> pilha das goroutines com frames de pkg.
func goroutinesWith(pkg string) map[string]string {
	buf := make([]byte, 4<<20)
	n := runtime.Stack(buf, true)
	out := map[string]string{}
	for _, g := range strings.Split(string(buf[:n]), "\n\n") {
		m := goroutineHeader.FindStringSubmatch(g)
		if m != nil && strings.Contains(g, pkg) {
			out[m[1]] = g
		}
	}
	return out
}

// leakCheck tira uma foto das goroutines de pkg agora; a função devolvida
// falha o teste se, depois de um período de tolerância, existirem goroutines
// novas de pkg (criadas durante o teste e nunca encerradas).
func leakCheck(t *testing.T, pkg string) func(what string) {
	t.Helper()
	before := goroutinesWith(pkg)
	return func(what string) {
		t.Helper()
		deadline := time.Now().Add(time.Second)
		for {
			var leaked []string
			for id, stack := range goroutinesWith(pkg) {
				if _, old := before[id]; !old {
					leaked = append(leaked, stack)
				}
			}
			if len(leaked) == 0 {
				return
			}
			if time.Now().After(deadline) {
				t.Errorf("%s: %d goroutine(s) de %s continuam vivas (vazamento):\n%s",
					what, len(leaked), strings.TrimSuffix(pkg, "."), strings.Join(leaked, "\n\n"))
				return
			}
			time.Sleep(20 * time.Millisecond)
		}
	}
}

// syncBuffer é um bytes.Buffer seguro para uso concorrente (destino de logs).
type syncBuffer struct {
	mu sync.Mutex
	b  bytes.Buffer
}

func (s *syncBuffer) Write(p []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.b.Write(p)
}

func (s *syncBuffer) String() string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.b.String()
}

// newLogger devolve um logger JSON que escreve em buf.
func newLogger() (*slog.Logger, *syncBuffer) {
	buf := &syncBuffer{}
	return slog.New(slog.NewJSONHandler(buf, nil)), buf
}

// logEntries decodifica as linhas JSON escritas pelo logger.
func logEntries(t *testing.T, s string) []map[string]any {
	t.Helper()
	var out []map[string]any
	for _, line := range strings.Split(strings.TrimSpace(s), "\n") {
		if line == "" {
			continue
		}
		var m map[string]any
		if err := json.Unmarshal([]byte(line), &m); err != nil {
			t.Fatalf("linha de log não é JSON (%v): %q", err, line)
		}
		out = append(out, m)
	}
	return out
}

// journalReport é o resultado da leitura de um arquivo de journal.
type journalReport struct {
	records []orders.Record
	broken  []string // linhas truncadas ou que não são JSON válido
}

// readJournal lê o journal e separa registros íntegros de linhas quebradas.
// Uma última linha sem '\n' conta como escrita parcial.
func readJournal(t *testing.T, path string) journalReport {
	t.Helper()
	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return journalReport{}
	}
	if err != nil {
		t.Fatalf("lendo o journal: %v", err)
	}
	var rep journalReport
	sc := bufio.NewScanner(bytes.NewReader(data))
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	for sc.Scan() {
		line := sc.Text()
		var rec orders.Record
		if err := json.Unmarshal([]byte(line), &rec); err != nil || rec.OrderID == "" {
			rep.broken = append(rep.broken, clip(line))
			continue
		}
		rep.records = append(rep.records, rec)
	}
	if len(data) > 0 && data[len(data)-1] != '\n' && len(rep.broken) == 0 {
		rep.broken = append(rep.broken, "(última linha sem quebra de linha final)")
	}
	return rep
}

func clip(s string) string {
	if len(s) > 120 {
		return s[:120] + "…"
	}
	return s
}

// sampleRecord monta um registro com alguns itens (linhas de ~300 bytes).
func sampleRecord(id string) orders.Record {
	return orders.Record{
		OrderID:  id,
		Customer: "cliente-" + id,
		Items: []orders.Item{
			{SKU: "CAFE-500G", Qty: 2, PriceCents: 3290},
			{SKU: "FILTRO-103", Qty: 1, PriceCents: 1190},
			{SKU: "CANECA-AZUL", Qty: 4, PriceCents: 2500},
		},
		TotalCents: 17770,
		At:         time.Date(2026, 10, 20, 12, 0, 0, 0, time.UTC),
	}
}
