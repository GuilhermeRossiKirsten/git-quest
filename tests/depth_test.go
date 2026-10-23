package tests

import (
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"runtime/debug"
	"strings"
	"testing"
	"time"

	"desafio31/src/formula"
)

func nest(open string, n int, inner, close string) string {
	return strings.Repeat(open, n) + inner + strings.Repeat(close, n)
}

func TestProfundidadeMaxima(t *testing.T) {
	if formula.MaxDepth != 200 {
		t.Fatalf("o contrato fixa MaxDepth = 200; o pacote declara %d", formula.MaxDepth)
	}
	env := baseEnv()
	runTable(t, env, []tcase{
		{nest("(", 200, "1", ")"), "1"},
		{nest("(", 201, "1", ")"), "deep@1:201"},
		{strings.Repeat("-", 200) + "1", "1"},
		{strings.Repeat("-", 201) + "1", "deep@1:201"},
		{strings.Repeat("!", 201) + "1", "deep@1:201"},
		{nest("abs(", 200, "1", ")"), "1"},
		{nest("abs(", 201, "1", ")"), "deep@1:804"},
		{nest("-(", 101, "1", ")"), "deep@1:201"},
		{strings.Repeat("(\n", 300) + "1", "deep@201:1"},
		{strings.Repeat("1 ^ ", 200) + "1", "1"},
		{strings.Repeat("2^", 300) + "2", "deep@1:402"},
	})

	// A mesma fórmula enorme, num processo filho com pilha limitada a 128 MiB:
	// um parser sem limite estoura a pilha (erro fatal, impossível de recuperar).
	ctx, cancel := context.WithTimeout(context.Background(), 90*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, os.Args[0], "-test.run=^TestProfundidadeFilho$", "-test.v", "-test.count=1")
	cmd.Env = append(os.Environ(), "GQ31_CHILD=1")
	out, err := cmd.CombinedOutput()
	if err != nil || !strings.Contains(string(out), "--- PASS: TestProfundidadeFilho") {
		all := string(out)
		tail := all
		if i := strings.Index(all, "runtime: goroutine stack exceeds"); i >= 0 {
			tail = all[i:] // a causa e o começo da pilha; o resto é ruído
		}
		if len(tail) > 1200 {
			tail = tail[:1200] + "…"
		}
		why := "falhou"
		if strings.Contains(all, "stack exceeds") || strings.Contains(all, "stack overflow") {
			why = "ESTOUROU A PILHA (fatal error: stack overflow)"
		} else if ctx.Err() != nil {
			why = "não terminou em 90 s"
		}
		t.Errorf("fórmulas gigantes (1 000 000 de '(' / de '-', soma com 20 000 termos) num processo com pilha de 128 MiB: o processo %s (%v):\n%s", why, err, tail)
	}
}

// TestProfundidadeFilho só roda como processo filho de TestProfundidadeMaxima.
func TestProfundidadeFilho(t *testing.T) {
	if os.Getenv("GQ31_CHILD") != "1" {
		t.Skip("auxiliar de TestProfundidadeMaxima")
	}
	debug.SetMaxStack(128 << 20)
	expectDeep := func(name, src string, col int) {
		_, err := formula.Parse(src)
		var fe *formula.Error
		if !errors.As(err, &fe) || fe.Kind != formula.KindTooDeep || fe.Pos != (formula.Pos{Line: 1, Col: col}) {
			t.Errorf("%s: esperado erro deep em 1:%d, obtido %v", name, col, outcome{err: err})
		}
	}
	expectDeep("1 000 000 de '('", strings.Repeat("(", 1_000_000), formula.MaxDepth+1)
	expectDeep("1 000 000 de '-'", strings.Repeat("-", 1_000_000)+"1", formula.MaxDepth+1)
	expectDeep("1 000 000 de 'max('", strings.Repeat("max(", 1_000_000), 4*(formula.MaxDepth+1))

	// Uma soma longa não é aninhamento: precisa ser aceita e somada.
	const terms = 20_000
	src := strings.Repeat("1 + ", terms-1) + "1"
	e, err := formula.Parse(src)
	if err != nil {
		t.Fatalf("soma com %d termos: Parse falhou: %v", terms, err)
	}
	v, err := formula.Eval(e, formula.Env{})
	if err != nil || v != terms {
		t.Errorf("soma com %d termos: esperado %d, obtido %v (erro %v)", terms, terms, v, err)
	}
	src = strings.Repeat("1 - ", terms-1) + "1"
	e, err = formula.Parse(src)
	if err != nil {
		t.Fatalf("subtração com %d termos: Parse falhou: %v", terms, err)
	}
	if v, err := formula.Eval(e, formula.Env{}); err != nil || v != 2-terms {
		t.Errorf("subtração com %d termos (da esquerda para a direita): esperado %d, obtido %v (erro %v)", terms, 2-terms, v, err)
	}
	fmt.Println("filho: ok")
}
