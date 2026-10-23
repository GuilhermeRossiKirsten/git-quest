package tests

import (
	"errors"
	"reflect"
	"testing"

	"desafio36/src/money"
)

// TestMoney_Centavos: dinheiro é contado em centavos inteiros — sem erro de
// ponto flutuante, sem misturar moedas e com arredondamento determinístico.
func TestMoney_Centavos(t *testing.T) {
	t.Run("New preserva os centavos", func(t *testing.T) {
		for _, c := range []int64{0, 1, 29, 57, 110, 1005, 123456789, -5} {
			m, err := money.New(c, brl)
			if err != nil {
				t.Fatalf("New(%d, BRL): erro inesperado %v", c, err)
			}
			if m.Cents() != c {
				t.Errorf("New(%d, BRL).Cents() = %d; o valor mudou no caminho (ponto flutuante?)", c, m.Cents())
			}
		}
	})

	t.Run("0.1 + 0.2 == 0.3", func(t *testing.T) {
		a, err1 := money.Parse("0.1", brl)
		b, err2 := money.Parse("0.2", brl)
		want, err3 := money.Parse("0.3", brl)
		if err1 != nil || err2 != nil || err3 != nil {
			t.Fatalf("Parse falhou: %v %v %v", err1, err2, err3)
		}
		sum, err := a.Add(b)
		if err != nil {
			t.Fatalf("Add: erro inesperado %v", err)
		}
		if sum.Cents() != 30 || !sum.Equal(want) || sum != want {
			t.Errorf("0.1 + 0.2 = %v (%d centavos); esperado %v, igual por Equal e por ==", sum, sum.Cents(), want)
		}
	})

	t.Run("dez vezes 0.10 dá exatamente 1.00", func(t *testing.T) {
		total := money.Zero(brl)
		dime, err := money.Parse("0.10", brl)
		if err != nil {
			t.Fatal(err)
		}
		for i := 0; i < 10; i++ {
			next, err := total.Add(dime)
			if err != nil {
				t.Fatal(err)
			}
			total = next
		}
		if total.Cents() != 100 || total != money.MustNew(100, brl) {
			t.Errorf("soma de dez 0.10 = %v (%d centavos); esperado BRL 1.00", total, total.Cents())
		}
	})

	t.Run("Parse aceita só centavos exatos", func(t *testing.T) {
		ok := map[string]int64{"12.34": 1234, "-0.05": -5, "7": 700, "0.5": 50, "0.29": 29, "1000000.01": 100000001}
		for in, want := range ok {
			m, err := money.Parse(in, brl)
			if err != nil || m.Cents() != want || m.Currency() != brl {
				t.Errorf("Parse(%q) = %v (%d centavos), %v; esperado %d centavos", in, m, m.Cents(), err, want)
			}
		}
		for _, in := range []string{"1.005", "abc", "", "1e3", "NaN", "Inf", "12,34", "0.1.2", "--1", "."} {
			if _, err := money.Parse(in, brl); !errors.Is(err, money.ErrInvalidAmount) {
				t.Errorf("Parse(%q): erro = %v; esperado money.ErrInvalidAmount", in, err)
			}
		}
	})

	t.Run("código de moeda ISO 4217 (3 letras maiúsculas)", func(t *testing.T) {
		for _, cur := range []string{"", "BR", "brl", "REAL", "B1L"} {
			if _, err := money.New(1, cur); !errors.Is(err, money.ErrInvalidCurrency) {
				t.Errorf("New(1, %q): erro = %v; esperado money.ErrInvalidCurrency", cur, err)
			}
		}
	})

	t.Run("moedas diferentes não se misturam", func(t *testing.T) {
		b, u := brlc(t, 100), usdc(t, 100)
		if r, err := b.Add(u); !errors.Is(err, money.ErrCurrencyMismatch) {
			t.Errorf("BRL 1.00 + USD 1.00 = %v, %v; esperado money.ErrCurrencyMismatch", r, err)
		}
		b, u = brlc(t, 100), usdc(t, 100)
		if r, err := b.Sub(u); !errors.Is(err, money.ErrCurrencyMismatch) {
			t.Errorf("BRL 1.00 - USD 1.00 = %v, %v; esperado money.ErrCurrencyMismatch", r, err)
		}
		if brlc(t, 100) == usdc(t, 100) || brlc(t, 100).Equal(usdc(t, 100)) {
			t.Errorf("BRL 1.00 e USD 1.00 não podem ser iguais")
		}
	})

	t.Run("Multiply e Percent com arredondamento meio-para-cima", func(t *testing.T) {
		if got := brlc(t, 29).Multiply(3).Cents(); got != 87 {
			t.Errorf("BRL 0.29 × 3 = %d centavos; esperado 87", got)
		}
		cases := []struct{ cents, pct, want int64 }{
			{999, 10, 100},   // 99,9 → 100
			{125, 10, 13},    // 12,5 → 13 (meio centavo sobe)
			{124, 10, 12},    // 12,4 → 12
			{1, 50, 1},       // 0,5 → 1
			{3333, 33, 1100}, // 1099,89 → 1100
			{105, 10, 11},    // 10,5 → 11
			{10000, 0, 0},
			{10000, 100, 10000},
			{-125, 10, -13}, // negativos: simétrico (afasta do zero)
		}
		for _, c := range cases {
			m := brlc(t, c.cents)
			if got := m.Percent(c.pct); got.Cents() != c.want || got.Currency() != brl {
				t.Errorf("%d centavos × %d%% = %v (%d centavos); esperado %d", c.cents, c.pct, got, got.Cents(), c.want)
			}
		}
	})

	t.Run("String", func(t *testing.T) {
		cases := map[int64]string{1234: "BRL 12.34", -5: "BRL -0.05", 0: "BRL 0.00", 100000001: "BRL 1000000.01"}
		for c, want := range cases {
			if got := brlc(t, c).String(); got != want {
				t.Errorf("String() de %d centavos = %q; esperado %q", c, got, want)
			}
		}
	})
}

// TestMoney_ValorImutavel: Money é um Value Object — operações devolvem um
// valor novo e nunca alteram os operandos.
func TestMoney_ValorImutavel(t *testing.T) {
	t.Run("Add e Sub não alteram os operandos", func(t *testing.T) {
		a, b := brlc(t, 100), brlc(t, 50)
		c, err := a.Add(b)
		if err != nil {
			t.Fatal(err)
		}
		if a.Cents() != 100 || b.Cents() != 50 || c.Cents() != 150 {
			t.Errorf("a=1.00, b=0.50; depois de c := a.Add(b): a=%v b=%v c=%v (esperado a=1.00 b=0.50 c=1.50)", a, b, c)
		}
		d, err := a.Sub(b)
		if err != nil {
			t.Fatal(err)
		}
		if a.Cents() != 100 || d.Cents() != 50 {
			t.Errorf("depois de d := a.Sub(b): a=%v d=%v (esperado a=1.00 d=0.50)", a, d)
		}
	})

	t.Run("Multiply e Percent não alteram o receptor", func(t *testing.T) {
		a := brlc(t, 1000)
		_ = a.Multiply(3)
		_ = a.Percent(10)
		if a.Cents() != 1000 {
			t.Errorf("o receptor mudou para %v", a)
		}
	})

	t.Run("cópias são independentes", func(t *testing.T) {
		a := brlc(t, 100)
		b := a
		if _, err := b.Add(brlc(t, 1)); err != nil {
			t.Fatal(err)
		}
		if a.Cents() != 100 || b.Cents() != 100 {
			t.Errorf("a=%v b=%v depois de b.Add(...) sem atribuir o resultado; esperado ambos BRL 1.00", a, b)
		}
	})

	t.Run("igualdade por valor (== e chave de map)", func(t *testing.T) {
		x, y := brlc(t, 1990), brlc(t, 1990)
		if x != y || !x.Equal(y) {
			t.Errorf("dois BRL 19.90 criados separadamente deveriam ser iguais")
		}
		seen := map[money.Money]int{x: 1}
		if seen[y] != 1 {
			t.Errorf("Money não funciona como chave de map por valor")
		}
	})

	t.Run("operações têm receptor por valor", func(t *testing.T) {
		// Um Value Object não precisa de ponteiro: se o método só existe em
		// *Money, ele pode alterar o valor — e money.MustNew(...).Add(x) nem compila.
		typ := reflect.TypeOf(money.Money{})
		for _, name := range []string{"Add", "Sub", "Multiply", "Percent", "Cents", "Currency", "Equal", "IsZero", "String"} {
			if _, ok := typ.MethodByName(name); !ok {
				t.Errorf("money.Money (valor) não tem o método %s — ele foi declarado com receptor *Money?", name)
			}
		}
	})
}
