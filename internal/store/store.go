// Package store é o "banco de dados" da loja: produtos e pedidos em memória,
// seguro para uso concorrente.
//
// Ele NÃO faz parte do que você pode modificar — trate-o como o banco que o
// time de dados mantém. Cada método é atômico por si só; combinar dois
// métodos (ler e depois gravar) NÃO é atômico.
//
// Hooks.BeforeWrite existe para os testes (e a demonstração) provocarem
// intercalações sem depender de sleeps: ele é chamado no início de toda
// escrita, antes de o Store adquirir sua trava.
package store

import (
	"errors"
	"fmt"
	"slices"
	"sync"
)

var (
	// ErrNotFound: o produto ou pedido não existe.
	ErrNotFound = errors.New("store: not found")
	// ErrVersionMismatch: ReplaceProductIfVersion encontrou outra versão gravada.
	ErrVersionMismatch = errors.New("store: version mismatch")
	// ErrOutOfStock: o pedido pede mais unidades do que há em estoque.
	ErrOutOfStock = errors.New("store: out of stock")
)

// Product é um produto do catálogo.
type Product struct {
	ID         string
	Name       string
	PriceCents int64 // preço em centavos de real
	Stock      int
	// Version começa em 1 e é incrementada a cada gravação do produto
	// (inclusive quando um pedido baixa o estoque).
	Version uint64
	// Seq é a ordem de criação: estritamente crescente, nunca reutilizada.
	Seq uint64
}

// Order é um pedido confirmado.
type Order struct {
	ID         string
	ProductID  string
	Quantity   int
	TotalCents int64
	Status     string
	Seq        uint64
}

// Op identifica uma operação de escrita para Hooks.BeforeWrite.
type Op string

// Operações de escrita.
const (
	OpCreateProduct  Op = "CreateProduct"
	OpReplaceProduct Op = "ReplaceProduct"
	OpDeleteProduct  Op = "DeleteProduct"
	OpCreateOrder    Op = "CreateOrder"
)

// Hooks permite observar (e pausar) escritas. Usado pelos testes.
type Hooks struct {
	// BeforeWrite, se não for nil, é chamado no início de toda escrita, antes
	// de o Store adquirir sua trava. id é o ID do produto envolvido (vazio
	// em CreateProduct).
	BeforeWrite func(op Op, id string)
}

// Store guarda produtos e pedidos. O valor zero não é utilizável: use New.
type Store struct {
	hooks Hooks

	mu       sync.Mutex
	seq      uint64
	products map[string]*Product
	orders   []Order
}

// New cria um Store vazio.
func New() *Store { return NewWithHooks(Hooks{}) }

// NewWithHooks cria um Store vazio com os ganchos dados.
func NewWithHooks(h Hooks) *Store {
	return &Store{hooks: h, products: map[string]*Product{}}
}

func (s *Store) before(op Op, id string) {
	if s.hooks.BeforeWrite != nil {
		s.hooks.BeforeWrite(op, id)
	}
}

// CreateProduct grava um produto novo; ID, Version e Seq são atribuídos aqui.
func (s *Store) CreateProduct(p Product) Product {
	s.before(OpCreateProduct, "")
	s.mu.Lock()
	defer s.mu.Unlock()
	s.seq++
	p.ID = fmt.Sprintf("prd_%04d", s.seq)
	p.Version = 1
	p.Seq = s.seq
	s.products[p.ID] = &p
	return p
}

// GetProduct devolve uma cópia do produto.
func (s *Store) GetProduct(id string) (Product, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	p, ok := s.products[id]
	if !ok {
		return Product{}, ErrNotFound
	}
	return *p, nil
}

// ListProducts devolve uma cópia de todos os produtos, do mais novo para o
// mais antigo (Seq decrescente).
func (s *Store) ListProducts() []Product {
	s.mu.Lock()
	out := make([]Product, 0, len(s.products))
	for _, p := range s.products {
		out = append(out, *p)
	}
	s.mu.Unlock()
	slices.SortFunc(out, func(a, b Product) int {
		switch {
		case a.Seq > b.Seq:
			return -1
		case a.Seq < b.Seq:
			return 1
		}
		return 0
	})
	return out
}

// ReplaceProduct substitui nome, preço e estoque do produto p.ID,
// incondicionalmente, e devolve o produto gravado (com a nova Version).
func (s *Store) ReplaceProduct(p Product) (Product, error) {
	s.before(OpReplaceProduct, p.ID)
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.replaceLocked(p)
}

// ReplaceProductIfVersion faz o mesmo que ReplaceProduct, mas só se a versão
// gravada ainda for version; caso contrário devolve ErrVersionMismatch e não
// altera nada. A comparação e a gravação são atômicas.
func (s *Store) ReplaceProductIfVersion(p Product, version uint64) (Product, error) {
	s.before(OpReplaceProduct, p.ID)
	s.mu.Lock()
	defer s.mu.Unlock()
	cur, ok := s.products[p.ID]
	if !ok {
		return Product{}, ErrNotFound
	}
	if cur.Version != version {
		return Product{}, ErrVersionMismatch
	}
	return s.replaceLocked(p)
}

func (s *Store) replaceLocked(p Product) (Product, error) {
	cur, ok := s.products[p.ID]
	if !ok {
		return Product{}, ErrNotFound
	}
	cur.Name, cur.PriceCents, cur.Stock = p.Name, p.PriceCents, p.Stock
	cur.Version++
	return *cur, nil
}

// DeleteProduct remove o produto.
func (s *Store) DeleteProduct(id string) error {
	s.before(OpDeleteProduct, id)
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, ok := s.products[id]; !ok {
		return ErrNotFound
	}
	delete(s.products, id)
	return nil
}

// CreateOrder baixa quantity unidades do estoque do produto e grava um
// pedido confirmado, atomicamente. quantity deve ser positiva (quem valida a
// entrada é a API).
func (s *Store) CreateOrder(productID string, quantity int) (Order, error) {
	s.before(OpCreateOrder, productID)
	s.mu.Lock()
	defer s.mu.Unlock()
	if quantity <= 0 {
		return Order{}, fmt.Errorf("store: invalid quantity %d", quantity)
	}
	p, ok := s.products[productID]
	if !ok {
		return Order{}, ErrNotFound
	}
	if p.Stock < quantity {
		return Order{}, ErrOutOfStock
	}
	p.Stock -= quantity
	p.Version++
	s.seq++
	o := Order{
		ID:         fmt.Sprintf("ord_%04d", s.seq),
		ProductID:  productID,
		Quantity:   quantity,
		TotalCents: p.PriceCents * int64(quantity),
		Status:     "confirmed",
		Seq:        s.seq,
	}
	s.orders = append(s.orders, o)
	return o, nil
}

// GetOrder devolve o pedido.
func (s *Store) GetOrder(id string) (Order, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, o := range s.orders {
		if o.ID == id {
			return o, nil
		}
	}
	return Order{}, ErrNotFound
}

// Orders devolve uma cópia de todos os pedidos, na ordem de criação.
func (s *Store) Orders() []Order {
	s.mu.Lock()
	defer s.mu.Unlock()
	return slices.Clone(s.orders)
}
