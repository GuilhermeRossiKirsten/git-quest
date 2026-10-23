// Package formula implementa a mini linguagem de fórmulas do motor de preços:
// números, variáveis, aritmética, comparações, lógica e chamadas de função.
//
//	e, err := formula.Parse("max(preco * (1 - desconto), piso)")
//	v, err := formula.Eval(e, formula.Env{Vars: map[string]float64{...}})
//
// A gramática, a tabela de precedência e as regras de erro estão no README do
// desafio; a API pública deste arquivo é usada pelos testes e não deve mudar.
package formula

import "fmt"

// Pos é uma posição no texto da fórmula: linha e coluna, ambas a partir de 1.
// A coluna conta caracteres (runas), não bytes.
type Pos struct {
	Line int
	Col  int
}

func (p Pos) String() string { return fmt.Sprintf("%d:%d", p.Line, p.Col) }

// Op identifica um operador. Unary usa OpNeg ou OpNot; Binary usa os demais.
type Op string

const (
	OpOr  Op = "||"
	OpAnd Op = "&&"
	OpEQ  Op = "=="
	OpNE  Op = "!="
	OpLT  Op = "<"
	OpLE  Op = "<="
	OpGT  Op = ">"
	OpGE  Op = ">="
	OpAdd Op = "+"
	OpSub Op = "-"
	OpMul Op = "*"
	OpDiv Op = "/"
	OpMod Op = "%"
	OpPow Op = "^"

	OpNeg Op = "-" // menos unário
	OpNot Op = "!" // negação lógica
)

// Expr é um nó da árvore sintática abstrata.
type Expr interface {
	// Pos devolve a posição do token que identifica o nó (veja cada tipo).
	Pos() Pos
	exprNode()
}

// Number é um literal numérico; At é a posição do primeiro caractere.
type Number struct {
	At    Pos
	Value float64
}

// Var é uma referência a variável; At é a posição do nome.
type Var struct {
	At   Pos
	Name string
}

// Unary é um operador prefixo (OpNeg ou OpNot); At é a posição do operador.
type Unary struct {
	At Pos
	Op Op
	X  Expr
}

// Binary é um operador binário; At é a posição do operador.
type Binary struct {
	At Pos
	Op Op
	L  Expr
	R  Expr
}

// Call é uma chamada de função; At é a posição do nome da função.
type Call struct {
	At   Pos
	Name string
	Args []Expr
}

func (n *Number) Pos() Pos { return n.At }
func (n *Var) Pos() Pos    { return n.At }
func (n *Unary) Pos() Pos  { return n.At }
func (n *Binary) Pos() Pos { return n.At }
func (n *Call) Pos() Pos   { return n.At }

func (*Number) exprNode() {}
func (*Var) exprNode()    {}
func (*Unary) exprNode()  {}
func (*Binary) exprNode() {}
func (*Call) exprNode()   {}
