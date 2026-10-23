package tests

import (
	"errors"
	"math/rand/v2"
	"strings"
	"testing"
	"unicode/utf8"

	"desafio31/src/formula"
)

// Variáveis do ambiente dos testes gerados (c = 0 provoca divisões por zero).
var genVars = map[string]float64{"a": 2, "b": 0.5, "c": 0, "d": -3}

var genNumbers = []float64{0, 1, 2, 3, 7, 10, 0.5, 1.5, 2.25, 1000, 1e21, 2.5e-7, 123456789, 3.14159}

var genOps = []formula.Op{
	formula.OpOr, formula.OpAnd, formula.OpEQ, formula.OpNE,
	formula.OpLT, formula.OpLE, formula.OpGT, formula.OpGE,
	formula.OpAdd, formula.OpSub, formula.OpMul, formula.OpDiv, formula.OpMod, formula.OpPow,
}

func genLeaf(r *rand.Rand, depth int) formula.Expr {
	switch k := r.IntN(20); {
	case k < 10:
		return &formula.Number{Value: genNumbers[r.IntN(len(genNumbers))]}
	case k < 17 || depth == 0:
		return &formula.Var{Name: string(rune('a' + r.IntN(4)))}
	default:
		name := []string{"min", "max", "abs"}[r.IntN(3)]
		n := 1
		if name != "abs" {
			n = 1 + r.IntN(3)
		}
		c := &formula.Call{Name: name}
		for range n {
			c.Args = append(c.Args, genExpr(r, depth-1))
		}
		return c
	}
}

// genExpr gera uma AST aleatória (sem posições: a impressora as preenche).
func genExpr(r *rand.Rand, depth int) formula.Expr {
	if depth <= 0 || r.IntN(5) == 0 {
		return genLeaf(r, depth)
	}
	if r.IntN(5) == 0 {
		op := formula.OpNeg
		if r.IntN(3) == 0 {
			op = formula.OpNot
		}
		return &formula.Unary{Op: op, X: genExpr(r, depth-1)}
	}
	return &formula.Binary{
		Op: genOps[r.IntN(len(genOps))],
		L:  genExpr(r, depth-1),
		R:  genExpr(r, depth-1),
	}
}

// Propriedade: para toda AST, Parse(imprimir com parênteses mínimos) devolve a
// mesma AST (com as posições dos tokens) e Eval concorda com o avaliador de
// referência.
func TestPropriedade_ImprimirERelerAST(t *testing.T) {
	r := rand.New(rand.NewPCG(31, 2026))
	env := formula.Env{Vars: genVars}
	const n = 4000
	astFail, evalFail := 0, 0
	for i := range n {
		ast := genExpr(r, 1+r.IntN(6))
		src := printMin(ast)
		ref := refEval(ast, genVars)

		if astFail < 5 {
			got, err, p := safeParse(src)
			switch {
			case p != "":
				astFail++
				t.Errorf("#%d Parse(%q) entrou em pânico: %s", i, src, p)
			case err != nil:
				astFail++
				t.Errorf("#%d Parse(%q) falhou: %v\n    AST esperada: %s", i, src, err, show(ast))
			default:
				if d := diffAST(ast, got); d != "" {
					astFail++
					t.Errorf("#%d Parse(%q): %s\n    AST esperada: %s\n    AST obtida:   %s", i, src, d, show(ast), show(got))
				} else if v, err, p := safeEval(got, env); p != "" {
					astFail++
					t.Errorf("#%d Eval(Parse(%q)) entrou em pânico: %s", i, src, p)
				} else if d := sameAsRef(ref, v, err); d != "" {
					astFail++
					t.Errorf("#%d Eval(Parse(%q)): %s", i, src, d)
				}
			}
		}
		if evalFail < 5 { // o avaliador sozinho, sobre a AST gerada
			v, err, p := safeEval(ast, env)
			if p != "" {
				evalFail++
				t.Errorf("#%d Eval(%s) entrou em pânico: %s", i, show(ast), p)
			} else if d := sameAsRef(ref, v, err); d != "" {
				evalFail++
				t.Errorf("#%d Eval(%s) [texto %q]: %s", i, show(ast), src, d)
			}
		}
	}
	if astFail+evalFail > 0 {
		t.Logf("parando após as primeiras diferenças (semente fixa: PCG(31, 2026), %d árvores)", n)
	}
}

var fuzzFragments = []string{
	"1", "2.5", ".5", "1e3", "1e", "1.", "..", "0", "x", "y", "min", "max", "abs", "nada",
	"(", ")", ",", "+", "-", "*", "/", "%", "^", "<", "<=", ">", ">=", "==", "!=", "!",
	"&&", "||", "&", "|", "=", " ", "  ", "\n", "\t", "é", "$", "e", "_", "1e999",
}

// posInBounds: a posição precisa apontar para um caractere da entrada ou para
// logo depois do último caractere de uma linha.
func posInBounds(src string, p formula.Pos) bool {
	lines := strings.Split(src, "\n")
	if p.Line < 1 || p.Line > len(lines) {
		return false
	}
	return p.Col >= 1 && p.Col <= utf8.RuneCountInString(lines[p.Line-1])+1
}

// Fuzz curto e determinístico: nenhuma entrada pode provocar pânico, todo erro
// é um *formula.Error com posição válida, e uma AST aceita sobrevive a
// imprimir-e-reler.
func TestFuzz_SemPanico(t *testing.T) {
	r := rand.New(rand.NewPCG(7, 31))
	env := formula.Env{Vars: map[string]float64{"x": 0, "y": 2}}
	const n = 20000
	problems, tried, accepted := 0, 0, 0
	report := func(format string, args ...any) {
		problems++
		if problems <= 8 {
			t.Errorf(format, args...)
		}
	}
	for range n {
		var b strings.Builder
		for range 1 + r.IntN(24) {
			if r.IntN(30) == 0 {
				b.WriteByte(byte(r.IntN(128)))
				continue
			}
			b.WriteString(fuzzFragments[r.IntN(len(fuzzFragments))])
		}
		src := b.String()
		if !utf8.ValidString(src) {
			continue
		}
		tried++
		e, err, p := safeParse(src)
		if p != "" {
			report("Parse(%q) entrou em pânico: %s", src, p)
			continue
		}
		if err != nil {
			var fe *formula.Error
			if !errors.As(err, &fe) {
				report("Parse(%q): erro %T não é *formula.Error: %v", src, err, err)
			} else if fe.Kind != formula.KindLex && fe.Kind != formula.KindSyntax && fe.Kind != formula.KindTooDeep {
				report("Parse(%q): tipo de erro inesperado %s", src, kindName(fe.Kind))
			} else if !posInBounds(src, fe.Pos) {
				report("Parse(%q): posição %s fora da entrada", src, fe.Pos)
			}
			continue
		}
		if e == nil {
			report("Parse(%q) devolveu AST nil sem erro", src)
			continue
		}
		accepted++
		if _, err, p := safeEval(e, env); p != "" {
			report("Eval(Parse(%q)) entrou em pânico: %s", src, p)
		} else if err != nil {
			var fe *formula.Error
			if !errors.As(err, &fe) || fe.Kind != formula.KindEval || !posInBounds(src, fe.Pos) {
				report("Eval(Parse(%q)): erro inválido %s", src, outcome{err: err})
			}
		}
		again := printMin(e) // a impressora reescreve as posições de e
		e2, err, p := safeParse(again)
		if p != "" || err != nil || show(e2) != show(e) {
			report("Parse(%q) = %s, mas reler %q deu %s %s", src, show(e), again, show(e2), outcome{err: err, panic: p})
		}
	}
	t.Logf("%d entradas testadas, %d aceitas por Parse", tried, accepted)
	if problems > 8 {
		t.Logf("... %d problemas no total (semente fixa: PCG(7, 31), %d entradas)", problems, n)
	}
}
