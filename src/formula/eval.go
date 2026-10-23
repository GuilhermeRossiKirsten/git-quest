package formula

import (
	"fmt"
	"math"
)

// Func é uma função disponível nas fórmulas. Os argumentos chegam já avaliados,
// da esquerda para a direita.
type Func func(args []float64) (float64, error)

// Env é o ambiente de avaliação. Funcs tem prioridade sobre as embutidas
// (min, max, abs).
type Env struct {
	Vars  map[string]float64
	Funcs map[string]Func
}

var builtins = map[string]func([]float64) float64{
	"min": func(a []float64) float64 {
		m := a[0]
		for _, v := range a[1:] {
			m = math.Min(m, v)
		}
		return m
	},
	"max": func(a []float64) float64 {
		m := a[0]
		for _, v := range a[1:] {
			m = math.Max(m, v)
		}
		return m
	},
	"abs": func(a []float64) float64 { return math.Abs(a[0]) },
}

func b2f(b bool) float64 {
	if b {
		return 1
	}
	return 0
}

// Eval avalia e no ambiente env. Verdadeiro é qualquer valor diferente de 0;
// comparações e operadores lógicos devolvem 1 ou 0.
func Eval(e Expr, env Env) (float64, error) {
	switch n := e.(type) {
	case *Number:
		return n.Value, nil
	case *Var:
		return env.Vars[n.Name], nil
	case *Unary:
		x, err := Eval(n.X, env)
		if err != nil {
			return 0, err
		}
		if n.Op == OpNot {
			return b2f(x == 0), nil
		}
		return -x, nil
	case *Binary:
		return evalBinary(n, env)
	case *Call:
		return evalCall(n, env)
	}
	return 0, fmt.Errorf("formula: nó desconhecido %T", e)
}

func evalBinary(n *Binary, env Env) (float64, error) {
	l, err := Eval(n.L, env)
	if err != nil {
		return 0, err
	}
	r, err := Eval(n.R, env)
	if err != nil {
		return 0, err
	}
	switch n.Op {
	case OpAnd:
		return b2f(l != 0 && r != 0), nil
	case OpOr:
		return b2f(l != 0 || r != 0), nil
	case OpEQ:
		return b2f(l == r), nil
	case OpNE:
		return b2f(l != r), nil
	case OpLT:
		return b2f(l < r), nil
	case OpLE:
		return b2f(l <= r), nil
	case OpGT:
		return b2f(l > r), nil
	case OpGE:
		return b2f(l >= r), nil
	case OpAdd:
		return l + r, nil
	case OpSub:
		return l - r, nil
	case OpMul:
		return l * r, nil
	case OpDiv:
		return l / r, nil
	case OpMod:
		return math.Mod(l, r), nil
	case OpPow:
		return math.Pow(l, r), nil
	}
	return 0, errorf(KindEval, n.At, "operador desconhecido %q", n.Op)
}

func evalCall(n *Call, env Env) (float64, error) {
	args := make([]float64, len(n.Args))
	for i, a := range n.Args {
		v, err := Eval(a, env)
		if err != nil {
			return 0, err
		}
		args[i] = v
	}
	if f, ok := env.Funcs[n.Name]; ok {
		v, err := f(args)
		if err != nil {
			return 0, &Error{Pos: n.At, Kind: KindEval, Msg: fmt.Sprintf("%s: %v", n.Name, err), Err: err}
		}
		return v, nil
	}
	fn, ok := builtins[n.Name]
	if !ok {
		return 0, errorf(KindEval, n.At, "função desconhecida %q", n.Name)
	}
	return fn(args), nil
}
