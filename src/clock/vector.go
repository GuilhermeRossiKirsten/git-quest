package clock

// Order é o resultado da comparação de dois relógios vetoriais.
type Order int

const (
	Equal      Order = iota // mesmos valores em todas as posições
	Before                  // v aconteceu antes de w (v → w)
	After                   // w aconteceu antes de v (w → v)
	Concurrent              // nenhum aconteceu antes do outro (v ∥ w)
)

func (o Order) String() string {
	switch o {
	case Equal:
		return "Equal"
	case Before:
		return "Before"
	case After:
		return "After"
	case Concurrent:
		return "Concurrent"
	}
	return "Order(?)"
}

// Vector é um relógio vetorial: a posição i conta eventos do processo i.
// Todos os vetores de um grupo têm o mesmo tamanho (o número de processos).
type Vector []uint64

// NewVector devolve um vetor zerado para n processos.
func NewVector(n int) Vector { return make(Vector, n) }

// Clone devolve uma cópia independente de v.
func (v Vector) Clone() Vector { return append(Vector(nil), v...) }

// Increment conta um evento do processo i (altera v).
func (v Vector) Increment(i int) { v[i]++ }

// Merge incorpora w em v, posição a posição, pelo máximo (altera v).
func (v Vector) Merge(w Vector) {
	for i := range v {
		v[i] = max(v[i], w[i])
	}
}

// Compare diz como v se relaciona com w na relação "aconteceu antes".
func (v Vector) Compare(w Vector) Order {
	// Igual a comparar números de versão: a primeira posição diferente decide.
	for i := range v {
		if v[i] < w[i] {
			return Before
		}
		if v[i] > w[i] {
			return After
		}
	}
	return Equal
}
