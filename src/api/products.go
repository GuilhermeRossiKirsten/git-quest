package api

import (
	"errors"
	"fmt"
	"net/http"
	"strconv"

	"desafio34/internal/store"
)

// productJSON é a representação de um produto.
//
// PR #412 ("dinheiro em centavos, chega de float"): price (reais, float) virou
// price_cents (inteiro). O app novo já foi atualizado.
type productJSON struct {
	ID         string `json:"id"`
	Name       string `json:"name"`
	PriceCents int64  `json:"price_cents"`
	Stock      int    `json:"stock"`
}

func toProductJSON(p store.Product) productJSON {
	return productJSON{ID: p.ID, Name: p.Name, PriceCents: p.PriceCents, Stock: p.Stock}
}

// productInput é o corpo de POST /products e PUT /products/{id}.
type productInput struct {
	Name       string `json:"name"`
	PriceCents int64  `json:"price_cents"`
	Stock      int    `json:"stock"`
}

func (in productInput) validate() error {
	if in.Name == "" {
		return errors.New("name is required")
	}
	if in.PriceCents < 0 || in.Stock < 0 {
		return errors.New("price_cents and stock must not be negative")
	}
	return nil
}

type productPage struct {
	Items []productJSON `json:"items"`
	Next  *string       `json:"next"`
}

// GET /products?limit=N — do mais novo para o mais antigo; siga "next".
func (s *Server) listProducts(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	limit, _ := strconv.Atoi(q.Get("limit"))
	if limit == 0 {
		limit = 20
	}
	offset, _ := strconv.Atoi(q.Get("offset"))

	all := s.store.ListProducts()
	if offset > len(all) {
		offset = len(all)
	}
	end := min(offset+limit, len(all))

	page := productPage{Items: []productJSON{}}
	for _, p := range all[offset:end] {
		page.Items = append(page.Items, toProductJSON(p))
	}
	if end < len(all) {
		next := fmt.Sprintf("/products?limit=%d&offset=%d", limit, end)
		page.Next = &next
	}
	writeJSON(w, http.StatusOK, page)
}

// POST /products
func (s *Server) createProduct(w http.ResponseWriter, r *http.Request) {
	var in productInput
	if err := decode(r, &in); err != nil {
		writeError(w, err)
		return
	}
	if err := in.validate(); err != nil {
		writeError(w, err)
		return
	}
	p := s.store.CreateProduct(store.Product{Name: in.Name, PriceCents: in.PriceCents, Stock: in.Stock})
	w.Header().Set("Location", "/products/"+p.ID)
	writeJSON(w, http.StatusCreated, toProductJSON(p))
}

// GET /products/{id}
func (s *Server) getProduct(w http.ResponseWriter, r *http.Request) {
	p, err := s.store.GetProduct(r.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		http.Error(w, "produto não encontrado", http.StatusNotFound)
		return
	}
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, toProductJSON(p))
}

// PUT /products/{id} — substitui nome, preço e estoque.
func (s *Server) replaceProduct(w http.ResponseWriter, r *http.Request) {
	var in productInput
	if err := decode(r, &in); err != nil {
		writeError(w, err)
		return
	}
	if err := in.validate(); err != nil {
		writeError(w, err)
		return
	}
	p, err := s.store.ReplaceProduct(store.Product{
		ID: r.PathValue("id"), Name: in.Name, PriceCents: in.PriceCents, Stock: in.Stock,
	})
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, toProductJSON(p))
}

// DELETE /products/{id}
func (s *Server) deleteProduct(w http.ResponseWriter, r *http.Request) {
	if err := s.store.DeleteProduct(r.PathValue("id")); err != nil {
		writeError(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}
