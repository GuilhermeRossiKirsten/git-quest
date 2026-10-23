package formula

// Analisador de descida recursiva, um procedimento por não-terminal:
//
//	expr    := sum { cmpop sum }
//	sum     := term [ ('+' | '-') sum ]
//	term    := power [ ('*' | '/' | '%') term ]
//	power   := unary { '^' unary }
//	unary   := ('-' | '!') unary | primary
//	primary := number | ident | ident '(' [ expr { ',' expr } ] ')' | '(' expr ')'

// cmpOps são os operadores de comparação e lógicos, tratados juntos em expr.
var cmpOps = map[string]bool{
	"<": true, "<=": true, ">": true, ">=": true, "==": true, "!=": true,
	"&&": true, "||": true,
}

type parser struct {
	toks []token
	i    int
}

// Parse analisa a fórmula e devolve sua AST.
func Parse(src string) (Expr, error) {
	toks, err := lex(src)
	if err != nil {
		return nil, err
	}
	p := &parser{toks: toks}
	return p.expr()
}

func (p *parser) peek() token { return p.toks[p.i] }

func (p *parser) next() token {
	t := p.toks[p.i]
	if t.kind != tEOF {
		p.i++
	}
	return t
}

func (p *parser) peekOp(ops ...string) (token, bool) {
	t := p.peek()
	if t.kind != tOp {
		return t, false
	}
	for _, op := range ops {
		if t.text == op {
			return t, true
		}
	}
	return t, false
}

func (p *parser) expr() (Expr, error) {
	left, err := p.sum()
	if err != nil {
		return nil, err
	}
	for {
		t := p.peek()
		if t.kind != tOp || !cmpOps[t.text] {
			return left, nil
		}
		p.next()
		right, err := p.sum()
		if err != nil {
			return nil, err
		}
		left = &Binary{At: t.pos, Op: Op(t.text), L: left, R: right}
	}
}

func (p *parser) sum() (Expr, error) {
	left, err := p.term()
	if err != nil {
		return nil, err
	}
	if t, ok := p.peekOp("+", "-"); ok {
		p.next()
		right, err := p.sum()
		if err != nil {
			return nil, err
		}
		return &Binary{At: t.pos, Op: Op(t.text), L: left, R: right}, nil
	}
	return left, nil
}

func (p *parser) term() (Expr, error) {
	left, err := p.power()
	if err != nil {
		return nil, err
	}
	if t, ok := p.peekOp("*", "/", "%"); ok {
		p.next()
		right, err := p.term()
		if err != nil {
			return nil, err
		}
		return &Binary{At: t.pos, Op: Op(t.text), L: left, R: right}, nil
	}
	return left, nil
}

func (p *parser) power() (Expr, error) {
	left, err := p.unary()
	if err != nil {
		return nil, err
	}
	for {
		t, ok := p.peekOp("^")
		if !ok {
			return left, nil
		}
		p.next()
		right, err := p.unary()
		if err != nil {
			return nil, err
		}
		left = &Binary{At: t.pos, Op: OpPow, L: left, R: right}
	}
}

func (p *parser) unary() (Expr, error) {
	if t, ok := p.peekOp("-", "!"); ok {
		p.next()
		x, err := p.unary()
		if err != nil {
			return nil, err
		}
		return &Unary{At: t.pos, Op: Op(t.text), X: x}, nil
	}
	return p.primary()
}

func (p *parser) primary() (Expr, error) {
	t := p.next()
	switch t.kind {
	case tNumber:
		return &Number{At: t.pos, Value: t.value}, nil
	case tIdent:
		if p.peek().kind != tLParen {
			return &Var{At: t.pos, Name: t.text}, nil
		}
		return p.call(t)
	case tLParen:
		e, err := p.expr()
		if err != nil {
			return nil, err
		}
		if err := p.expect(tRParen, "')'"); err != nil {
			return nil, err
		}
		return e, nil
	}
	return nil, errorf(KindSyntax, t.pos, "esperava número, variável, função ou '(', encontrou %s", t.describe())
}

func (p *parser) call(name token) (Expr, error) {
	p.next() // '('
	c := &Call{At: name.pos, Name: name.text}
	if p.peek().kind != tRParen {
		for {
			arg, err := p.expr()
			if err != nil {
				return nil, err
			}
			c.Args = append(c.Args, arg)
			if p.peek().kind != tComma {
				break
			}
			p.next()
		}
	}
	if err := p.expect(tRParen, "',' ou ')'"); err != nil {
		return nil, err
	}
	return c, nil
}

func (p *parser) expect(k tokenKind, what string) error {
	t := p.peek()
	if t.kind != k {
		return errorf(KindSyntax, t.pos, "esperava %s, encontrou %s", what, t.describe())
	}
	p.next()
	return nil
}
