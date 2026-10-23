package formula

import (
	"strconv"
	"strings"
)

type tokenKind int

const (
	tEOF tokenKind = iota
	tNumber
	tIdent
	tOp     // operadores
	tLParen // (
	tRParen // )
	tComma  // ,
)

type token struct {
	kind  tokenKind
	text  string
	value float64 // só para tNumber
	pos   Pos
}

// describe é o nome do token nas mensagens de erro.
func (t token) describe() string {
	if t.kind == tEOF {
		return "o fim da fórmula"
	}
	return strconv.Quote(t.text)
}

const singleCharOps = "+-*/%^<>!"

// lex quebra a fórmula em tokens. As fórmulas da planilha cabem numa linha,
// então a posição é simplesmente o deslocamento + 1.
func lex(src string) ([]token, error) {
	var toks []token
	i := 0
	for i < len(src) {
		c := src[i]
		pos := Pos{Line: 1, Col: i + 1}
		switch {
		case c == ' ' || c == '\t' || c == '\r' || c == '\n':
			i++
		case isDigit(c):
			start := i
			for i < len(src) && isDigit(src[i]) {
				i++
			}
			if i < len(src) && src[i] == '.' {
				i++
				if i >= len(src) || !isDigit(src[i]) {
					return nil, errorf(KindLex, pos, "número malformado %q", src[start:i])
				}
				for i < len(src) && isDigit(src[i]) {
					i++
				}
			}
			v, err := strconv.ParseFloat(src[start:i], 64)
			if err != nil {
				return nil, errorf(KindLex, pos, "número malformado %q", src[start:i])
			}
			toks = append(toks, token{kind: tNumber, text: src[start:i], value: v, pos: Pos{Line: 1, Col: start + 1}})
		case isLetter(c):
			start := i
			for i < len(src) && (isLetter(src[i]) || isDigit(src[i])) {
				i++
			}
			toks = append(toks, token{kind: tIdent, text: src[start:i], pos: pos})
		case c == '(':
			toks = append(toks, token{kind: tLParen, text: "(", pos: pos})
			i++
		case c == ')':
			toks = append(toks, token{kind: tRParen, text: ")", pos: pos})
			i++
		case c == ',':
			toks = append(toks, token{kind: tComma, text: ",", pos: pos})
			i++
		case strings.IndexByte(singleCharOps, c) >= 0:
			toks = append(toks, token{kind: tOp, text: string(c), pos: pos})
			i++
		default:
			return nil, errorf(KindLex, pos, "caractere inesperado %q", c)
		}
	}
	toks = append(toks, token{kind: tEOF, pos: Pos{Line: 1, Col: len(src) + 1}})
	return toks, nil
}

func isDigit(c byte) bool  { return c >= '0' && c <= '9' }
func isLetter(c byte) bool { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') }
