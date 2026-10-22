// Package catalog é o serviço de catálogo de produtos das lojas: lê do Redis
// quando pode e do banco (lento) quando precisa — o padrão cache-aside.
//
// Várias réplicas deste serviço rodam ao mesmo tempo, todas com o mesmo Redis
// e o mesmo banco.
package catalog

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"gitquest/catalog/internal/productdb"
	"gitquest/catalog/internal/redisx"
)

// Store é o banco de produtos, a fonte da verdade (implementado por *productdb.DB).
type Store interface {
	// Get devolve productdb.ErrNotFound se o produto não existe.
	Get(ctx context.Context, tenant, id string) (productdb.Product, error)
	Save(ctx context.Context, p productdb.Product) (productdb.Product, error)
}

// cacheTTL é por quanto tempo um produto fica no cache.
const cacheTTL = 10 * time.Minute

// Service é seguro para uso concorrente.
type Service struct {
	cache *redisx.Client
	db    Store
}

// New cria o serviço. cache e db são compartilhados com as outras réplicas.
func New(cache *redisx.Client, db Store) *Service {
	return &Service{cache: cache, db: db}
}

// GetProduct devolve o produto da loja. Se ele não existe, o erro satisfaz
// errors.Is(err, productdb.ErrNotFound).
func (s *Service) GetProduct(ctx context.Context, tenant, id string) (productdb.Product, error) {
	key := productKey(tenant, id)

	raw, ok, err := s.cache.Get(ctx, key)
	if err != nil {
		return productdb.Product{}, fmt.Errorf("lendo o cache: %w", err)
	}
	if ok {
		var p productdb.Product
		if err := json.Unmarshal([]byte(raw), &p); err != nil {
			return productdb.Product{}, fmt.Errorf("decodificando %s: %w", key, err)
		}
		return p, nil
	}

	// Cache miss: busca no banco e guarda para os próximos.
	p, err := s.db.Get(ctx, tenant, id)
	if err != nil {
		return productdb.Product{}, err
	}
	data, err := json.Marshal(p)
	if err != nil {
		return productdb.Product{}, err
	}
	if err := s.cache.Set(ctx, key, string(data), cacheTTL); err != nil {
		return productdb.Product{}, fmt.Errorf("gravando no cache: %w", err)
	}
	return p, nil
}

// SaveProduct cria ou atualiza o produto (nome, preço). Quando ela retorna
// sem erro, as leituras seguintes — desta ou de outra réplica — precisam ver
// o produto novo.
func (s *Service) SaveProduct(ctx context.Context, p productdb.Product) error {
	// Tira o produto do cache antes de gravar, para ninguém ler o preço antigo.
	if _, err := s.cache.Del(ctx, productKey(p.Tenant, p.ID)); err != nil {
		return fmt.Errorf("invalidando o cache: %w", err)
	}
	if _, err := s.db.Save(ctx, p); err != nil {
		return fmt.Errorf("gravando o produto: %w", err)
	}
	return nil
}
