// Command calc avalia fórmulas com o pacote src/formula e mostra a AST com
// todos os parênteses explícitos — a forma mais rápida de ver como o parser
// agrupou cada operador.
//
//	go run ./cmd/calc '2 - 3 - 4'              # uma fórmula
//	go run ./cmd/calc 'preco * qtd' preco=9.9 qtd=3
//	go run ./cmd/calc -demo                    # os chamados do suporte, um por um
//
// Este programa não faz parte do que você deve alterar.
package main

import (
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"

	"desafio31/src/formula"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "uso: calc 'fórmula' [nome=valor ...]  |  calc -demo")
		os.Exit(2)
	}
	if os.Args[1] == "-demo" {
		demo()
		return
	}
	env := formula.Env{Vars: map[string]float64{}}
	for _, kv := range os.Args[2:] {
		name, val, ok := strings.Cut(kv, "=")
		v, err := strconv.ParseFloat(val, 64)
		if !ok || err != nil {
			fmt.Fprintf(os.Stderr, "variável inválida %q (use nome=valor)\n", kv)
			os.Exit(2)
		}
		env.Vars[name] = v
	}
	fmt.Println(run(os.Args[1], env))
}

// run devolve "AST => valor" ou a descrição do erro.
func run(src string, env formula.Env) (out string) {
	defer func() {
		if r := recover(); r != nil {
			out = fmt.Sprintf("PÂNICO: %v", r)
		}
	}()
	e, err := formula.Parse(src)
	if err != nil {
		return describeErr(err)
	}
	v, err := formula.Eval(e, env)
	if err != nil {
		return fmt.Sprintf("AST %s => %s", show(e), describeErr(err))
	}
	return fmt.Sprintf("AST %s => %s", show(e), strconv.FormatFloat(v, 'g', -1, 64))
}

func describeErr(err error) string {
	var fe *formula.Error
	if errors.As(err, &fe) {
		return fmt.Sprintf("erro %s em %d:%d: %s", fe.Kind, fe.Pos.Line, fe.Pos.Col, fe.Msg)
	}
	return "erro: " + err.Error()
}

// show imprime a AST com um par de parênteses em volta de cada operação.
func show(e formula.Expr) string {
	switch n := e.(type) {
	case *formula.Number:
		return strconv.FormatFloat(n.Value, 'g', -1, 64)
	case *formula.Var:
		return n.Name
	case *formula.Unary:
		return "(" + string(n.Op) + show(n.X) + ")"
	case *formula.Binary:
		return "(" + show(n.L) + " " + string(n.Op) + " " + show(n.R) + ")"
	case *formula.Call:
		args := make([]string, len(n.Args))
		for i, a := range n.Args {
			args[i] = show(a)
		}
		return n.Name + "(" + strings.Join(args, ", ") + ")"
	}
	return fmt.Sprintf("%T", e)
}

func demo() {
	calls := 0
	env := formula.Env{
		Vars: map[string]float64{"preco": 80, "qtd": 0, "desconto": 0.1},
		Funcs: map[string]formula.Func{
			// avisa: registra um alerta no painel (efeito colateral) e devolve 1.
			"avisa": func([]float64) (float64, error) { calls++; return 1, nil },
		},
	}
	cases := []struct{ src, want string }{
		{"preco * (1 - desconto)", "72"},
		{"100 - 10 - 5", "85 (as subtrações valem da esquerda para a direita)"},
		{"2 ^ 3 ^ 2", "512 (potência agrupa da direita: 2^(3^2))"},
		{"-2 ^ 2", "-4 (a potência é mais forte que o menos unário)"},
		{"1 + 2 < 4 && 5 > 3", "1"},
		{"1.5e3 + 1", "1501"},
		{".5 + 1.", "1.5"},
		{"preco >= 100 || qtd != 0", "0"},
		{"preco 2", "erro sintático em 1:7 (sobrou um token)"},
		{"preco +\n  * 2", "erro sintático em 2:3"},
		{"qtd != 0 && preco / qtd > 10", "0, sem dividir por zero"},
		{"preco / qtd", "erro de avaliação em 1:7 (divisão por zero)"},
		{"precoo * 2", "erro de avaliação em 1:1 (variável desconhecida)"},
		{"1 || avisa()", "1, sem chamar avisa()"},
		{"min()", "erro de avaliação em 1:1 (número de argumentos)"},
	}
	for _, c := range cases {
		before := calls
		got := run(c.src, env)
		if calls != before {
			got += fmt.Sprintf("  [avisa() chamada %d vez(es)]", calls-before)
		}
		fmt.Printf("fórmula:  %q\n  esperado: %s\n  obtido:   %s\n", c.src, c.want, got)
	}
	deep := strings.Repeat("(", 5000) + "1" + strings.Repeat(")", 5000)
	fmt.Printf("fórmula:  \"((((…1…))))\" com 5000 níveis\n  esperado: erro de aninhamento em 1:%d (limite MaxDepth = %d)\n  obtido:   %s\n",
		formula.MaxDepth+1, formula.MaxDepth, run(deep, env))
}
