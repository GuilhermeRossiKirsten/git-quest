package tests

import (
	"errors"
	"fmt"
	"math"
	"strconv"
	"strings"
	"testing"

	"desafio31/src/formula"
)

// ---------------------------------------------------------------------------
// Execução protegida: um pânico no código avaliado vira falha do caso, não a
// morte da suíte inteira.

type outcome struct {
	val   float64
	err   error
	panic string
}

func safeParse(src string) (e formula.Expr, err error, panicMsg string) {
	defer func() {
		if r := recover(); r != nil {
			panicMsg = fmt.Sprint(r)
		}
	}()
	e, err = formula.Parse(src)
	return e, err, ""
}

func safeEval(e formula.Expr, env formula.Env) (v float64, err error, panicMsg string) {
	defer func() {
		if r := recover(); r != nil {
			panicMsg = fmt.Sprint(r)
		}
	}()
	v, err = formula.Eval(e, env)
	return v, err, ""
}

// run faz Parse e, se der certo, Eval.
func run(src string, env formula.Env) outcome {
	e, err, p := safeParse(src)
	if p != "" {
		return outcome{panic: "Parse entrou em pânico: " + p}
	}
	if err != nil {
		return outcome{err: err}
	}
	v, err, p := safeEval(e, env)
	if p != "" {
		return outcome{panic: "Eval entrou em pânico: " + p}
	}
	return outcome{val: v, err: err}
}

func (o outcome) String() string {
	switch {
	case o.panic != "":
		return o.panic
	case o.err != nil:
		var fe *formula.Error
		if errors.As(o.err, &fe) {
			return fmt.Sprintf("erro %s em %d:%d (%s)", kindName(fe.Kind), fe.Pos.Line, fe.Pos.Col, fe.Msg)
		}
		return fmt.Sprintf("erro %T que não é *formula.Error: %v", o.err, o.err)
	}
	return "valor " + fmtNum(o.val)
}

func fmtNum(v float64) string { return strconv.FormatFloat(v, 'g', -1, 64) }

// ---------------------------------------------------------------------------
// Tabelas de casos. want é um número ("7", "-0.5") ou "tipo@linha:coluna"
// (tipos: lex, syntax, deep, eval).

type tcase struct {
	src  string
	want string
}

var kindNames = map[formula.Kind]string{
	formula.KindLex:     "lex",
	formula.KindSyntax:  "syntax",
	formula.KindTooDeep: "deep",
	formula.KindEval:    "eval",
}

func kindName(k formula.Kind) string {
	if n, ok := kindNames[k]; ok {
		return n
	}
	return fmt.Sprintf("Kind(%d)", int(k))
}

func describeWant(want string) string {
	if kind, at, ok := strings.Cut(want, "@"); ok {
		return fmt.Sprintf("erro %s em %s", kind, at)
	}
	return "valor " + want
}

// check confere um caso; devolve "" se passou ou a descrição da diferença.
func check(c tcase, env formula.Env) string {
	got := run(c.src, env)
	if got.panic != "" {
		return got.panic
	}
	if kind, at, isErr := strings.Cut(c.want, "@"); isErr {
		if got.err == nil {
			return "obtido " + got.String()
		}
		var fe *formula.Error
		if !errors.As(got.err, &fe) {
			return "obtido " + got.String()
		}
		if kindName(fe.Kind) != kind || fe.Pos.String() != at {
			return "obtido " + got.String()
		}
		return ""
	}
	want, err := strconv.ParseFloat(c.want, 64)
	if err != nil {
		panic("caso de teste inválido: " + c.want)
	}
	if got.err != nil || !approxEqual(got.val, want) {
		return "obtido " + got.String()
	}
	return ""
}

func approxEqual(a, b float64) bool {
	if a == b {
		return true
	}
	return math.Abs(a-b) <= 1e-12*math.Max(math.Abs(a), math.Abs(b))
}

// abbrev cita a fórmula; as gigantes (testes de profundidade) são resumidas.
func abbrev(src string) string {
	r := []rune(src)
	if len(r) <= 90 {
		return strconv.Quote(src)
	}
	return fmt.Sprintf("%s…%s (%d caracteres)", strconv.Quote(string(r[:40])), strconv.Quote(string(r[len(r)-20:])), len(r))
}

func runTable(t *testing.T, env formula.Env, cases []tcase) {
	t.Helper()
	failed := 0
	for _, c := range cases {
		if msg := check(c, env); msg != "" {
			failed++
			t.Errorf("%s: esperado %s, %s", abbrev(c.src), describeWant(c.want), msg)
		}
	}
	if failed > 0 {
		t.Logf("%d de %d casos falharam", failed, len(cases))
	}
}

// baseEnv é o ambiente das tabelas.
func baseEnv() formula.Env {
	return formula.Env{Vars: map[string]float64{
		"x": 3, "y": 0.5, "preco": 80, "qtd": 0, "desconto": 0.1,
	}}
}

// ---------------------------------------------------------------------------
// Impressora de referência: escreve a AST com o MÍNIMO de parênteses segundo a
// tabela de precedência do README, e registra em cada nó (campo At) a posição
// em que o token dele foi escrito — assim Parse(texto) deve devolver
// exatamente a mesma árvore, posições incluídas.

// precedência de cada nível (maior = agrupa primeiro).
var binPrec = map[formula.Op]int{
	formula.OpOr: 1, formula.OpAnd: 2,
	formula.OpEQ: 3, formula.OpNE: 3,
	formula.OpLT: 4, formula.OpLE: 4, formula.OpGT: 4, formula.OpGE: 4,
	formula.OpAdd: 5, formula.OpSub: 5,
	formula.OpMul: 6, formula.OpDiv: 6, formula.OpMod: 6,
	formula.OpPow: 8,
}

const (
	unaryPrec = 7
	atomPrec  = 9
)

func nodePrec(e formula.Expr) int {
	switch n := e.(type) {
	case *formula.Binary:
		return binPrec[n.Op]
	case *formula.Unary:
		return unaryPrec
	}
	return atomPrec
}

type printer struct{ b strings.Builder }

func (p *printer) here() formula.Pos { return formula.Pos{Line: 1, Col: p.b.Len() + 1} }

// printMin imprime e (ASCII, uma linha) e devolve o texto.
func printMin(e formula.Expr) string {
	var p printer
	p.expr(e)
	return p.b.String()
}

func (p *printer) child(e formula.Expr, parens bool) {
	if parens {
		p.b.WriteByte('(')
	}
	p.expr(e)
	if parens {
		p.b.WriteByte(')')
	}
}

func (p *printer) expr(e formula.Expr) {
	switch n := e.(type) {
	case *formula.Number:
		n.At = p.here()
		p.b.WriteString(fmtNum(n.Value))
	case *formula.Var:
		n.At = p.here()
		p.b.WriteString(n.Name)
	case *formula.Call:
		n.At = p.here()
		p.b.WriteString(n.Name + "(")
		for i, a := range n.Args {
			if i > 0 {
				p.b.WriteString(", ")
			}
			p.expr(a)
		}
		p.b.WriteByte(')')
	case *formula.Unary:
		n.At = p.here()
		p.b.WriteString(string(n.Op))
		p.child(n.X, nodePrec(n.X) < unaryPrec)
	case *formula.Binary:
		prec := binPrec[n.Op]
		if n.Op == formula.OpPow { // associativo à direita; o direito pode ser unário
			p.child(n.L, nodePrec(n.L) <= prec)
			p.b.WriteByte(' ')
			n.At = p.here()
			p.b.WriteString("^ ")
			p.child(n.R, nodePrec(n.R) < unaryPrec)
			return
		}
		p.child(n.L, nodePrec(n.L) < prec)
		p.b.WriteByte(' ')
		n.At = p.here()
		p.b.WriteString(string(n.Op) + " ")
		p.child(n.R, nodePrec(n.R) <= prec)
	default:
		panic(fmt.Sprintf("nó inesperado %T", e))
	}
}

// show imprime a AST com todos os parênteses (para mensagens de erro).
func show(e formula.Expr) string {
	switch n := e.(type) {
	case nil:
		return "<nil>"
	case *formula.Number:
		return fmtNum(n.Value)
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

// diffAST devolve "" se as árvores são iguais (estrutura, operadores, valores
// e posições) ou a primeira diferença encontrada.
func diffAST(want, got formula.Expr) string {
	if fmt.Sprintf("%T", want) != fmt.Sprintf("%T", got) {
		return fmt.Sprintf("esperado nó %s, obtido %s", show(want), show(got))
	}
	if want.Pos() != got.Pos() {
		return fmt.Sprintf("nó %s: posição esperada %s, obtida %s", show(want), want.Pos(), got.Pos())
	}
	switch w := want.(type) {
	case *formula.Number:
		if g := got.(*formula.Number); g.Value != w.Value {
			return fmt.Sprintf("número esperado %s, obtido %s", fmtNum(w.Value), fmtNum(g.Value))
		}
	case *formula.Var:
		if g := got.(*formula.Var); g.Name != w.Name {
			return fmt.Sprintf("variável esperada %s, obtida %s", w.Name, g.Name)
		}
	case *formula.Unary:
		g := got.(*formula.Unary)
		if g.Op != w.Op {
			return fmt.Sprintf("em %s: operador %q, obtido %q", show(want), w.Op, g.Op)
		}
		return diffAST(w.X, g.X)
	case *formula.Binary:
		g := got.(*formula.Binary)
		if g.Op != w.Op {
			return fmt.Sprintf("esperado %s, obtido %s", show(want), show(got))
		}
		if d := diffAST(w.L, g.L); d != "" {
			return d
		}
		return diffAST(w.R, g.R)
	case *formula.Call:
		g := got.(*formula.Call)
		if g.Name != w.Name || len(g.Args) != len(w.Args) {
			return fmt.Sprintf("esperado %s, obtido %s", show(want), show(got))
		}
		for i := range w.Args {
			if d := diffAST(w.Args[i], g.Args[i]); d != "" {
				return d
			}
		}
	}
	return ""
}

// ---------------------------------------------------------------------------
// Avaliador de referência, escrito a partir do contrato do README.

type refResult struct {
	val float64
	err bool
	pos formula.Pos
}

func refFail(pos formula.Pos) refResult { return refResult{err: true, pos: pos} }

func b2f(b bool) float64 {
	if b {
		return 1
	}
	return 0
}

func finite(v float64) bool { return !math.IsNaN(v) && !math.IsInf(v, 0) }

func refEval(e formula.Expr, vars map[string]float64) refResult {
	switch n := e.(type) {
	case *formula.Number:
		return refResult{val: n.Value}
	case *formula.Var:
		v, ok := vars[n.Name]
		if !ok {
			return refFail(n.At)
		}
		return refResult{val: v}
	case *formula.Unary:
		x := refEval(n.X, vars)
		if x.err {
			return x
		}
		if n.Op == formula.OpNot {
			return refResult{val: b2f(x.val == 0)}
		}
		return refResult{val: -x.val}
	case *formula.Call:
		args := make([]float64, len(n.Args))
		for i, a := range n.Args {
			r := refEval(a, vars)
			if r.err {
				return r
			}
			args[i] = r.val
		}
		switch {
		case n.Name == "abs" && len(args) == 1:
			return refResult{val: math.Abs(args[0])}
		case n.Name == "min" && len(args) >= 1:
			m := args[0]
			for _, v := range args[1:] {
				m = math.Min(m, v)
			}
			return refResult{val: m}
		case n.Name == "max" && len(args) >= 1:
			m := args[0]
			for _, v := range args[1:] {
				m = math.Max(m, v)
			}
			return refResult{val: m}
		}
		return refFail(n.At)
	case *formula.Binary:
		l := refEval(n.L, vars)
		if l.err {
			return l
		}
		switch n.Op {
		case formula.OpAnd:
			if l.val == 0 {
				return refResult{val: 0}
			}
			r := refEval(n.R, vars)
			if r.err {
				return r
			}
			return refResult{val: b2f(r.val != 0)}
		case formula.OpOr:
			if l.val != 0 {
				return refResult{val: 1}
			}
			r := refEval(n.R, vars)
			if r.err {
				return r
			}
			return refResult{val: b2f(r.val != 0)}
		}
		r := refEval(n.R, vars)
		if r.err {
			return r
		}
		a, b := l.val, r.val
		var v float64
		switch n.Op {
		case formula.OpEQ:
			return refResult{val: b2f(a == b)}
		case formula.OpNE:
			return refResult{val: b2f(a != b)}
		case formula.OpLT:
			return refResult{val: b2f(a < b)}
		case formula.OpLE:
			return refResult{val: b2f(a <= b)}
		case formula.OpGT:
			return refResult{val: b2f(a > b)}
		case formula.OpGE:
			return refResult{val: b2f(a >= b)}
		case formula.OpAdd:
			v = a + b
		case formula.OpSub:
			v = a - b
		case formula.OpMul:
			v = a * b
		case formula.OpDiv:
			if b == 0 {
				return refFail(n.At)
			}
			v = a / b
		case formula.OpMod:
			if b == 0 {
				return refFail(n.At)
			}
			v = math.Mod(a, b)
		case formula.OpPow:
			v = math.Pow(a, b)
		}
		if !finite(v) {
			return refFail(n.At)
		}
		return refResult{val: v}
	}
	panic(fmt.Sprintf("nó inesperado %T", e))
}

// sameAsRef compara o resultado de formula.Eval com o de referência.
func sameAsRef(ref refResult, v float64, err error) string {
	if ref.err {
		var fe *formula.Error
		if err == nil || !errors.As(err, &fe) || fe.Kind != formula.KindEval || fe.Pos != ref.pos {
			return fmt.Sprintf("esperado erro eval em %s, obtido %s", ref.pos, outcome{val: v, err: err})
		}
		return ""
	}
	if err != nil || v != ref.val {
		return fmt.Sprintf("esperado valor %s, obtido %s", fmtNum(ref.val), outcome{val: v, err: err})
	}
	return ""
}
