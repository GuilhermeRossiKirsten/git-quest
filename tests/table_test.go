package tests

import (
	"errors"
	"strings"
	"testing"

	"desafio31/src/formula"
)

func TestCaminhoFeliz(t *testing.T) {
	runTable(t, baseEnv(), []tcase{
		{"1 + 2", "3"},
		{"preco * 2", "160"},
		{"(2 + 3) * 4", "20"},
		{"max(3, 7)", "7"},
		{"min(preco, 50)", "50"},
		{"abs(-5)", "5"},
		{"10 / 4", "2.5"},
		{"-3", "-3"},
		{"preco > 50", "1"},
		{"x * x", "9"},
		{"1.25 + 0.75", "2"},
	})
}

func TestLexico_Numeros(t *testing.T) {
	runTable(t, baseEnv(), []tcase{
		{"1.5e3", "1500"},
		{"1.5E3", "1500"},
		{".5", "0.5"},
		{"1.", "1"},
		{"1.e2", "100"},
		{"2e-2", "0.02"},
		{"2.5e+2", "250"},
		{"007", "7"},
		{"0.000", "0"},
		{"  42  ", "42"},
		{".5 + 1.", "1.5"},
		{"1.5e3*2", "3000"},
		{"12345678901234567890", "12345678901234567890"},
		{"1e", "lex@1:1"},
		{"1e+", "lex@1:1"},
		{"2 * 1e-", "lex@1:5"},
		{"1.2.3", "lex@1:1"},
		{"12abc", "lex@1:1"},
		{"1 + 3x", "lex@1:5"},
		{".", "lex@1:1"},
		{"..5", "lex@1:1"},
		{"1..2", "lex@1:1"},
		{"1e400", "lex@1:1"},
		{"x + .e5", "lex@1:5"},
		{"1_000", "lex@1:1"},
	})
}

func TestLexico_Operadores(t *testing.T) {
	runTable(t, baseEnv(), []tcase{
		{"1 <= 1", "1"},
		{"2 >= 3", "0"},
		{"2 == 2", "1"},
		{"2 != 2", "0"},
		{"1 && 0", "0"},
		{"0 || 3", "1"},
		{"!0", "1"},
		{"!!5", "1"},
		{"!5", "0"},
		{"1<=2", "1"},
		{"x<-y", "0"},
		{"x==3&&y!=0", "1"},
		{"1 = 1", "lex@1:3"},
		{"1 & 1", "lex@1:3"},
		{"1 | 0", "lex@1:3"},
		{"1 =< 2", "lex@1:3"},
		{"1 !== 1", "lex@1:5"},
		{"1 >= = 2", "lex@1:6"},
		{"1 <> 2", "syntax@1:4"},
		{"x ! y", "syntax@1:3"},
	})
}

func TestPosicoes(t *testing.T) {
	env := baseEnv()
	runTable(t, env, []tcase{
		{"1 +\n  * 2", "syntax@2:3"},
		{"x\n\n  + $", "lex@3:5"},
		{"\tmax(1,\n2", "syntax@2:2"},
		{"(1 + 2", "syntax@1:7"},
		{"1 + 2)", "syntax@1:6"},
		{"1 +\r\n * 2", "syntax@2:2"},
		{"a + é", "lex@1:5"},
		{"1 +\n 2 +\n  3 +", "syntax@3:6"},
		{"1 +\n", "syntax@2:1"},
		{" \n\n", "syntax@3:1"},
		{"1 + 2 \n", "3"},
		{"preco +\n  10 / qtd", "eval@2:6"},
		{"1 +\n\tnada", "eval@2:2"},
	})

	_, err, p := safeParse("1 +\n  * 2")
	var fe *formula.Error
	switch {
	case p != "":
		t.Errorf("Parse entrou em pânico: %s", p)
	case !errors.As(err, &fe):
		t.Errorf("Parse(%q): esperado *formula.Error, obtido %v", "1 +\n  * 2", err)
	case !strings.HasPrefix(fe.Error(), "2:3: ") || strings.Contains(fe.Error(), "%!"):
		t.Errorf("Error() deve começar com \"linha:coluna: \" e trazer a mensagem; obtido %q", fe.Error())
	}
}

func TestPrecedencia(t *testing.T) {
	runTable(t, baseEnv(), []tcase{
		{"1 + 2 * 3", "7"},
		{"2 * 3 + 1", "7"},
		{"(1 + 2) * 3", "9"},
		{"1 + 6 / 3", "3"},
		{"10 - 4 % 3", "9"},
		{"2 * 3 ^ 2", "18"},
		{"2 ^ 3 * 2", "16"},
		{"1 + 2 < 4", "1"},
		{"1 + 1 == 2", "1"},
		{"2 == 1 < 3", "0"},
		{"1 || 0 && 0", "1"},
		{"1 < 2 && 3 < 2", "0"},
		{"!0 + 1", "2"},
		{"!(1 + 1 == 2)", "0"},
		{"!1 == 0", "1"},
		{"-x + 1", "-2"},
		{"2 + 3 > 4 == 1", "1"},
		{"1 - 2 < 0 || 0", "1"},
		{"preco * (1 - desconto) > 70 && qtd == 0", "1"},
	})
}

func TestAssociatividadeEUnarios(t *testing.T) {
	runTable(t, baseEnv(), []tcase{
		{"2 - 3 - 4", "-5"},
		{"2 - 3 + 4", "3"},
		{"100 / 10 / 5", "2"},
		{"8 / 2 * 4", "16"},
		{"17 % 5 % 3", "2"},
		{"2 ^ 3 ^ 2", "512"},
		{"(2 ^ 3) ^ 2", "64"},
		{"2 ^ 2 ^ 3 ^ 0", "4"},
		{"4 ^ 0.5 ^ 2", "1.4142135623730951"},
		{"2 ^ -1", "0.5"},
		{"2 ^ -1 ^ 2", "0.5"},
		{"-2 ^ 2", "-4"},
		{"-(2 ^ 2)", "-4"},
		{"(-2) ^ 2", "4"},
		{"-x ^ 2", "-9"},
		{"- -2", "2"},
		{"--2", "2"},
		{"2 * -3", "-6"},
		{"-2 * 3", "-6"},
		{"1 - -1", "2"},
		{"3 > 2 > 1", "0"},
		{"1 < 2 < 3", "1"},
	})
}

func TestEntradaInteira(t *testing.T) {
	runTable(t, baseEnv(), []tcase{
		{"1 2", "syntax@1:3"},
		{"(1) (2)", "syntax@1:5"},
		{"max(1, 2) x", "syntax@1:11"},
		{"1.5 e3", "syntax@1:5"},
		{"2 3 + 4", "syntax@1:3"},
		{"x y", "syntax@1:3"},
		{"(1 + 2))", "syntax@1:8"},
		{"", "syntax@1:1"},
		{"   ", "syntax@1:4"},
		{")", "syntax@1:1"},
		{"()", "syntax@1:2"},
		{"1 +", "syntax@1:4"},
		{"+1", "syntax@1:1"},
		{"1 * / 2", "syntax@1:5"},
		{"max(1,)", "syntax@1:7"},
		{"max(,1)", "syntax@1:5"},
		{"max(1 2)", "syntax@1:7"},
	})
}

// tickEnv registra as chamadas de tick(n) (efeito colateral observável).
func tickEnv(calls *[]float64) formula.Env {
	env := baseEnv()
	env.Funcs = map[string]formula.Func{
		"tick": func(a []float64) (float64, error) {
			*calls = append(*calls, a...)
			if len(a) == 0 {
				return 0, nil
			}
			return a[0], nil
		},
		"fail": func([]float64) (float64, error) { return 0, errBoom },
	}
	return env
}

var errBoom = errors.New("serviço de câmbio fora do ar")

func TestCurtoCircuito(t *testing.T) {
	cases := []struct {
		src   string
		want  string
		calls string
	}{
		{"0 && tick(1)", "0", ""},
		{"1 || tick(1)", "1", ""},
		{"1 && tick(2)", "1", "2"},
		{"0 || tick(0)", "0", "0"},
		{"tick(1) && tick(0) && tick(3)", "0", "1 0"},
		{"tick(0) || tick(2) || tick(3)", "1", "0 2"},
		{"!(0 && tick(9))", "1", ""},
		{"qtd != 0 && 10 / qtd > 2", "0", ""},
		{"qtd == 0 || 10 % qtd", "1", ""},
		{"qtd > 0 && fail()", "0", ""},
		{"2 && 3", "1", ""},
		{"0 || 5", "1", ""},
		{"tick(1) + tick(2) * tick(3)", "7", "1 2 3"},
		{"max(tick(5), tick(4))", "5", "5 4"},
	}
	for _, c := range cases {
		var calls []float64
		msg := check(tcase{c.src, c.want}, tickEnv(&calls))
		parts := make([]string, len(calls))
		for i, v := range calls {
			parts[i] = fmtNum(v)
		}
		got := strings.Join(parts, " ")
		if msg != "" {
			t.Errorf("%q: esperado %s, %s", c.src, describeWant(c.want), msg)
		}
		if got != c.calls {
			t.Errorf("%q: tick() deveria ser chamada com [%s] (nessa ordem), foi com [%s]", c.src, c.calls, got)
		}
	}
}

func TestErrosDeAvaliacao(t *testing.T) {
	var calls []float64
	env := tickEnv(&calls)
	runTable(t, env, []tcase{
		{"1 / 0", "eval@1:3"},
		{"10 % (2 - 2)", "eval@1:4"},
		{"x / (y - 0.5)", "eval@1:3"},
		{"nada + 1", "eval@1:1"},
		{"preco * precoo", "eval@1:9"},
		{"foo(1)", "eval@1:1"},
		{"min()", "eval@1:1"},
		{"max()", "eval@1:1"},
		{"abs()", "eval@1:1"},
		{"abs(1, 2)", "eval@1:1"},
		{"10 ^ 400", "eval@1:4"},
		{"1e308 * 10", "eval@1:7"},
		{"(-8) ^ 0.5", "eval@1:6"},
		{"2 * fail()", "eval@1:5"},
		{"0 / 5", "0"},
		{"-7 % 3", "-1"},
		{"7 % -3", "1"},
		{"0 ^ 0", "1"},
		{"min(3)", "3"},
		{"max(1, 5, 3)", "5"},
	})

	got := run("1 + fail(2)", env)
	if got.panic != "" {
		t.Fatal(got.panic)
	}
	if !errors.Is(got.err, errBoom) {
		t.Errorf("erro devolvido por uma Func deve ser preservado (errors.Is); obtido %s", got)
	}
}
