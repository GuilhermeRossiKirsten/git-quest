// Package api expõe o catálogo e os pedidos da loja como uma API HTTP JSON.
//
// Quem consome: o app novo da loja (time interno) e o ERP do distribuidor
// parceiro — um cliente "v1" escrito por uma consultoria em 2023, que ninguém
// mais pode alterar. O contrato público está no README do desafio.
package api

import (
	"net/http"

	"desafio34/internal/store"
)

// Server atende a API. Crie com New.
type Server struct {
	store *store.Store
	mux   *http.ServeMux
}

// New devolve o handler HTTP da API sobre o store dado.
func New(st *store.Store) http.Handler {
	s := &Server{store: st, mux: http.NewServeMux()}
	s.mux.HandleFunc("GET /products", s.listProducts)
	s.mux.HandleFunc("POST /products", s.createProduct)
	s.mux.HandleFunc("GET /products/{id}", s.getProduct)
	s.mux.HandleFunc("PUT /products/{id}", s.replaceProduct)
	s.mux.HandleFunc("DELETE /products/{id}", s.deleteProduct)
	s.mux.HandleFunc("POST /orders", s.createOrder)
	s.mux.HandleFunc("GET /orders/{id}", s.getOrder)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s.mux.ServeHTTP(w, r)
}
