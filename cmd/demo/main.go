// Demonstração dos sintomas relatados pelo suporte. Usa só a API pública dos
// pacotes de domínio e mostra, para cada cenário, o esperado e o observado.
package main

import (
	"fmt"

	"desafio36/src/money"
	"desafio36/src/ordering"
)

func report(scenario, want, got string) {
	mark := "ok"
	if want != got {
		mark = "DIVERGE"
	}
	fmt.Printf("  [%-7s] %s\n            esperado: %s\n            observado: %s\n", mark, scenario, want, got)
}

func errText(err error) string {
	if err == nil {
		return "aceito"
	}
	return "rejeitado"
}

func main() {
	fmt.Println("Cenários do suporte (pedido com limite de crédito de BRL 100.00):")

	o, _ := ordering.NewOrder("pedido-1", "cliente-1", money.MustNew(10000, "BRL"))
	_ = o.AddLine("CAFE", 1, money.MustNew(29, "BRL"))
	report("item de BRL 0.29 — total em centavos", "29", fmt.Sprint(o.Total().Cents()))

	_ = o.AddLine("CANECA", 2, money.MustNew(1500, "BRL"))
	_ = o.ChangeQuantity("CANECA", 3)
	report("caneca 2 → 3 unidades de BRL 15.00 — subtotal", "BRL 45.29", o.Subtotal().String())

	_ = o.ApplyDiscount(10)
	_ = o.Confirm()
	report("cupom de 10% e confirmação — total cobrado", "BRL 40.76", o.Total().String())
	report("incluir item depois de confirmado", "rejeitado", errText(o.AddLine("BOLO", 1, money.MustNew(900, "BRL"))))

	p, _ := ordering.NewOrder("pedido-2", "cliente-1", money.MustNew(10000, "BRL"))
	report("item de BRL 500.00 com limite de BRL 100.00", "rejeitado", errText(p.AddLine("TV", 1, money.MustNew(50000, "BRL"))))
	report("item com preço em USD num pedido em BRL", "rejeitado", errText(p.AddLine("LIVRO", 1, money.MustNew(1000, "USD"))))
	report("quantidade zero", "rejeitado", errText(p.AddLine("NADA", 0, money.MustNew(100, "BRL"))))
	p.PullEvents()
	report("segundo PullEvents seguido — eventos", "0", fmt.Sprint(len(p.PullEvents())))

	a, _ := money.Parse("0.1", "BRL")
	b, _ := money.Parse("0.2", "BRL")
	sum, _ := a.Add(b)
	report("0.1 + 0.2 (e a continua valendo 0.1?)", "BRL 0.30 / a=BRL 0.10", fmt.Sprintf("%v / a=%v", sum, a))
	fmt.Println()
	fmt.Println("Rode ./test.sh para ver cada regra do contrato (README) com ✔/✘.")
}
