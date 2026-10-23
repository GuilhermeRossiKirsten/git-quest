package tests

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"regexp"
	"strings"
	"testing"

	"desafio36/src/money"
	"desafio36/src/ordering"
)

// Programa-sonda: usa apenas a API pública. O controle (sem linha extra) TEM
// de compilar; cada caso acrescenta UMA linha que contorna o agregado e NÃO
// pode compilar — se compila, qualquer código da empresa pode fazer o mesmo.
const probeTemplate = `package probe

import (
	"desafio36/src/money"
	"desafio36/src/ordering"
)

func Probe() {
	limit := money.MustNew(100000, "BRL")
	o, _ := ordering.NewOrder("p-1", "c-1", limit)
	_ = o.AddLine("A", 1, money.MustNew(1000, "BRL"))
	m := money.MustNew(10, "BRL")
	_, _ = o, m
	%s
}
`

var probeCases = []struct{ name, line string }{
	{"atribuir o status do pedido", `o.State = ordering.StatusShipped`},
	{"trocar a lista de itens", `o.Items = nil`},
	{"sobrescrever o total", `o.TotalAmount = m`},
	{"zerar os eventos pendentes", `o.Events = nil`},
	{"alterar a quantidade de um item por fora", `o.Lines()[0].Qty = 0`},
	{"alterar o valor de um Money", `m.Amount = 0.3`},
	{"montar Money com campos", `_ = money.Money{Amount: 1, Code: "BRL"}`},
	{"montar Order com campos", `_ = ordering.Order{OrderID: "x", State: ordering.StatusConfirmed}`},
}

// moduleRoot devolve a raiz do módulo desafio36 (os testes rodam em tests/).
func moduleRoot(t *testing.T) string {
	t.Helper()
	wd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	root := filepath.Dir(wd)
	if _, err := os.Stat(filepath.Join(root, "go.mod")); err != nil {
		t.Fatalf("go.mod não encontrado em %s", root)
	}
	return root
}

// buildProbe compila um programa-sonda num módulo temporário que aponta para
// o código do participante (replace). Devolve a saída do compilador.
func buildProbe(t *testing.T, root, line string) (string, error) {
	t.Helper()
	dir := t.TempDir()
	gomod := fmt.Sprintf("module probe\n\ngo 1.25\n\nrequire desafio36 v0.0.0\n\nreplace desafio36 => %s\n", filepath.ToSlash(root))
	if err := os.WriteFile(filepath.Join(dir, "go.mod"), []byte(gomod), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "probe.go"), []byte(fmt.Sprintf(probeTemplate, line)), 0o644); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command("go", "build", "./...")
	cmd.Dir = dir
	cmd.Env = append(os.Environ(), "GOFLAGS=-buildvcs=false -mod=mod", "GOPROXY=off", "GOWORK=off")
	out, err := cmd.CombinedOutput()
	return string(out), err
}

// TestEncapsulamento_NaoCompila: o estado do agregado e do Money só muda
// através dos métodos — código de fora que tenta mexer nos campos não compila.
func TestEncapsulamento_NaoCompila(t *testing.T) {
	if _, err := exec.LookPath("go"); err != nil {
		t.Fatalf("ferramenta go não encontrada no PATH: %v", err)
	}
	root := moduleRoot(t)
	if out, err := buildProbe(t, root, "// controle: só a API pública"); err != nil {
		t.Fatalf("o programa de controle, que usa só a API pública do README, não compilou — a API mudou?\n%s", out)
	}
	// A linha extra é a 14ª do arquivo; o erro tem de apontar para ela.
	lineRef := regexp.MustCompile(`probe\.go:14:`)
	for _, c := range probeCases {
		t.Run(c.name, func(t *testing.T) {
			out, err := buildProbe(t, root, c.line)
			if err == nil {
				t.Errorf("compilou: `%s` — código de fora do pacote consegue burlar as regras", c.line)
				return
			}
			if !lineRef.MatchString(out) {
				t.Errorf("a compilação falhou por outro motivo (esperado erro na linha da sonda):\n%s", out)
			}
		})
	}
}

// TestEncapsulamento_EstadoNaoVaza: nada do que a API devolve é uma janela
// para o estado interno (sem campos exportados, sem slices compartilhados).
func TestEncapsulamento_EstadoNaoVaza(t *testing.T) {
	t.Run("sem campos exportados", func(t *testing.T) {
		for _, typ := range []reflect.Type{
			reflect.TypeOf(ordering.Order{}),
			reflect.TypeOf(ordering.OrderLine{}),
			reflect.TypeOf(money.Money{}),
		} {
			var exported []string
			for i := 0; i < typ.NumField(); i++ {
				if f := typ.Field(i); f.IsExported() {
					exported = append(exported, f.Name)
				}
			}
			if len(exported) > 0 {
				t.Errorf("%s expõe campos que qualquer pacote pode alterar: %s", typ, strings.Join(exported, ", "))
			}
		}
	})

	t.Run("Lines devolve uma cópia", func(t *testing.T) {
		o := newOrder(t, 1000000)
		must(t, "AddLine(A)", o.AddLine("A", 1, brlc(t, 100)))
		must(t, "AddLine(B)", o.AddLine("B", 2, brlc(t, 200)))
		before := view(o)
		ls := o.Lines()
		if len(ls) != 2 {
			t.Fatalf("Lines() tem %d itens; esperado 2", len(ls))
		}
		ls[0], ls[1] = ls[1], ls[0]
		_ = append(ls[:1], ls[0])
		if after := view(o); !sameView(before, after) {
			t.Errorf("alterar o slice devolvido por Lines() alterou o pedido:\n  antes:  %v\n  depois: %v", before, after)
		}
	})

	t.Run("PullEvents devolve uma cópia", func(t *testing.T) {
		o := newOrder(t, 1000000)
		must(t, "AddLine(A)", o.AddLine("A", 1, brlc(t, 100)))
		evs := o.PullEvents()
		if len(evs) != 1 {
			t.Fatalf("PullEvents() = %v; esperado [LineAdded]", eventNames(evs))
		}
		evs[0] = ordering.OrderShipped{OrderID: "falso"}
		must(t, "AddLine(B)", o.AddLine("B", 1, brlc(t, 100)))
		next := o.PullEvents()
		if names := eventNames(next); len(names) != 1 || names[0] != "LineAdded" {
			t.Errorf("depois de alterar o slice devolvido, o próximo PullEvents() = %v; esperado [LineAdded]", names)
		}
	})
}
