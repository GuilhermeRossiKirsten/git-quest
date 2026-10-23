// Package money representa valores monetários usados pelo contexto de pedidos.
//
// Herdado do primeiro protótipo da loja: o valor é guardado em unidades da
// moeda (reais, dólares) e a moeda pelo código ISO 4217.
package money

import (
	"errors"
	"fmt"
	"strconv"
)

var (
	// ErrCurrencyMismatch indica uma operação entre moedas diferentes.
	ErrCurrencyMismatch = errors.New("money: moedas diferentes")
	// ErrInvalidCurrency indica um código de moeda inválido.
	ErrInvalidCurrency = errors.New("money: moeda inválida")
	// ErrInvalidAmount indica um valor textual que não representa centavos exatos.
	ErrInvalidAmount = errors.New("money: valor inválido")
)

// Money é um valor monetário.
type Money struct {
	Amount float64 // em unidades da moeda (12.34 = doze reais e trinta e quatro centavos)
	Code   string  // ISO 4217: "BRL", "USD", ...
}

// New cria um valor a partir de centavos.
func New(cents int64, currency string) (Money, error) {
	if len(currency) != 3 {
		return Money{}, fmt.Errorf("%w: %q", ErrInvalidCurrency, currency)
	}
	return Money{Amount: float64(cents) / 100, Code: currency}, nil
}

// MustNew é como New, mas entra em pânico se os argumentos forem inválidos.
func MustNew(cents int64, currency string) Money {
	m, err := New(cents, currency)
	if err != nil {
		panic(err)
	}
	return m
}

// Zero devolve o valor zero na moeda indicada.
func Zero(currency string) Money {
	return Money{Code: currency}
}

// Parse lê um valor decimal como "12.34" ou "-0.05".
func Parse(amount, currency string) (Money, error) {
	f, err := strconv.ParseFloat(amount, 64)
	if err != nil {
		return Money{}, fmt.Errorf("%w: %q", ErrInvalidAmount, amount)
	}
	if len(currency) != 3 {
		return Money{}, fmt.Errorf("%w: %q", ErrInvalidCurrency, currency)
	}
	return Money{Amount: f, Code: currency}, nil
}

// Cents devolve o valor em centavos.
func (m Money) Cents() int64 { return int64(m.Amount * 100) }

// Currency devolve o código ISO 4217 da moeda.
func (m Money) Currency() string { return m.Code }

// IsZero informa se o valor é zero.
func (m Money) IsZero() bool { return m.Amount == 0 }

// Equal informa se os dois valores são iguais (mesmo valor e mesma moeda).
func (m Money) Equal(o Money) bool { return m == o }

// Add soma o e devolve o resultado.
func (m *Money) Add(o Money) (Money, error) {
	m.Amount += o.Amount
	return *m, nil
}

// Sub subtrai o e devolve o resultado.
func (m *Money) Sub(o Money) (Money, error) {
	if m.Code != o.Code {
		return Money{}, ErrCurrencyMismatch
	}
	m.Amount -= o.Amount
	return *m, nil
}

// Multiply devolve o valor multiplicado por n (preço unitário × quantidade).
func (m Money) Multiply(n int64) Money {
	return Money{Amount: m.Amount * float64(n), Code: m.Code}
}

// Percent devolve p% do valor (usado para descontos).
func (m Money) Percent(p int64) Money {
	return Money{Amount: m.Amount * float64(p) / 100, Code: m.Code}
}

// String formata como "BRL 12.34".
func (m Money) String() string {
	return fmt.Sprintf("%s %.2f", m.Code, m.Amount)
}
