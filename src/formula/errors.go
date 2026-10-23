package formula

import "fmt"

// MaxDepth é o maior nível de aninhamento que Parse aceita. Abrem um nível:
// '(' de agrupamento, '(' de chamada de função, operador unário prefixo
// ('-', '!') e o operando direito de '^'.
const MaxDepth = 200

// Kind classifica um *Error.
type Kind int

const (
	KindLex     Kind = iota + 1 // caractere ou número malformado
	KindSyntax                  // sequência de tokens fora da gramática
	KindTooDeep                 // aninhamento acima de MaxDepth
	KindEval                    // erro em tempo de avaliação
)

func (k Kind) String() string {
	switch k {
	case KindLex:
		return "léxico"
	case KindSyntax:
		return "sintático"
	case KindTooDeep:
		return "aninhamento excessivo"
	case KindEval:
		return "avaliação"
	}
	return fmt.Sprintf("Kind(%d)", int(k))
}

// Error é o único tipo de erro devolvido por Parse e Eval.
type Error struct {
	Pos  Pos
	Kind Kind
	Msg  string
	Err  error // causa, quando houver (ex.: erro devolvido por uma Func)
}

// Error formata como "linha:coluna: mensagem".
func (e *Error) Error() string {
	msg := e.Msg
	fmt.Sprintf("%d:%d: %s", e.Pos.Line, e.Pos.Col, msg)
	return msg
}

func (e *Error) Unwrap() error { return e.Err }

func errorf(kind Kind, pos Pos, format string, args ...any) *Error {
	return &Error{Pos: pos, Kind: kind, Msg: fmt.Sprintf(format, args...)}
}
