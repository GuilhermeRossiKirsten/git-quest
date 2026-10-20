// Comando report lê a saída de `go test -json` na entrada padrão e imprime
// um critério de conclusão por linha (✔/✘), com o motivo de cada falha.
// Sai com código 1 se algum critério falhar.
package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

type event struct {
	Action string
	Test   string
	Output string
}

// criteria associa cada teste de nível superior a um critério do README.
var criteria = []struct{ test, label string }{
	{"TestEdgeCases", "casos de borda: workers <= 0 é rejeitado; entradas vazias não são erro"},
	{"TestConcurrencyLimit", "no máximo `workers` chamadas simultâneas de fn (e todas as vagas são usadas)"},
	{"TestPreservesOrder", "results[i] corresponde a inputs[i]"},
	{"TestFirstError", "primeiro erro: cancela as demais chamadas, para de despachar e é devolvido (errors.Is)"},
	{"TestCallerCancellation", "cancelamento/deadline do chamador interrompe o lote em até 200ms"},
	{"TestNoGoroutineLeak", "nenhuma goroutine sobrevive ao retorno de Run (sucesso, erro e cancelamento)"},
	{"TestStress", "lote de 2000 itens sem resultados perdidos"},
}

const maxLines = 30

func main() {
	status := map[string]string{}   // teste de nível superior -> pass/fail/skip
	output := map[string][]string{} // teste de nível superior -> linhas relevantes
	var loose []string              // saída fora de qualquer teste (build, pânico, timeout)
	var race []string               // primeiro relatório do detector de corridas
	races, inRace := 0, false

	sc := bufio.NewScanner(os.Stdin)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	for sc.Scan() {
		var ev event
		if err := json.Unmarshal(sc.Bytes(), &ev); err != nil {
			loose = append(loose, sc.Text()) // ex.: erro de compilação em texto puro
			continue
		}
		top, _, _ := strings.Cut(ev.Test, "/")
		switch ev.Action {
		case "pass", "fail", "skip":
			if ev.Test != "" && !strings.Contains(ev.Test, "/") {
				status[top] = ev.Action
			}
		case "output", "build-output":
			line := strings.TrimRight(ev.Output, "\n")
			trimmed := strings.TrimSpace(line)
			// Relatórios do -race vêm entre linhas "=================="; eles
			// aparecem uma única vez, no critério "sem data races".
			switch {
			case trimmed == "==================":
				inRace = false
				continue
			case strings.Contains(line, "WARNING: DATA RACE"):
				inRace = true
				races++
			}
			if inRace {
				if races == 1 && len(race) < 25 {
					race = append(race, line)
				}
				continue
			}
			if strings.HasPrefix(trimmed, "=== ") || strings.HasPrefix(trimmed, "--- PASS") ||
				strings.HasPrefix(trimmed, "--- FAIL") {
				continue
			}
			if ev.Test == "" {
				loose = append(loose, line)
			} else {
				output[top] = append(output[top], line)
			}
		}
	}

	failed := 0
	for _, c := range criteria {
		switch status[c.test] {
		case "pass":
			fmt.Printf("  ✔ %s\n", c.label)
		case "":
			failed++
			fmt.Printf("  ✘ %s\n      não terminou: pânico, timeout ou erro de compilação (veja a saída abaixo)\n", c.label)
		default:
			failed++
			fmt.Printf("  ✘ %s\n", c.label)
			printLines(output[c.test])
		}
	}

	if races > 0 {
		failed++
		fmt.Printf("  ✘ sem data races (go test -race): %d relatório(s); o primeiro:\n", races)
		printLines(race)
	} else if len(status) > 0 {
		fmt.Printf("  ✔ sem data races (go test -race)\n")
	} else {
		failed++
		fmt.Printf("  ✘ sem data races (go test -race): os testes não chegaram a rodar\n")
	}

	if len(status) < len(criteria) && len(loose) > 0 {
		fmt.Println("\n  Saída do go test fora dos testes:")
		printLines(loose)
	}

	fmt.Printf("\n  %d de %d critérios de teste satisfeitos.\n", len(criteria)+1-failed, len(criteria)+1)
	if failed > 0 {
		os.Exit(1)
	}
}

func printLines(lines []string) {
	shown := 0
	for _, l := range lines {
		if strings.TrimSpace(l) == "" {
			continue
		}
		if shown == maxLines {
			fmt.Printf("      … (%d linhas omitidas; log completo em build/test.json)\n", len(lines)-shown)
			return
		}
		fmt.Printf("      %s\n", l)
		shown++
	}
}
