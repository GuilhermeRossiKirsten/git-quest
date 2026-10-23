package api

import (
	"errors"
	"net/http"

	"desafio34/internal/store"
)

// orderInput é o corpo de POST /orders.
//
// PR #412 também padronizou "qty" como no app novo.
type orderInput struct {
	ProductID string `json:"product_id"`
	Qty       int    `json:"qty"`
}

// orderJSON é a representação de um pedido.
type orderJSON struct {
	ID         string `json:"id"`
	ProductID  string `json:"product_id"`
	Qty        int    `json:"qty"`
	TotalCents int64  `json:"total_cents"`
	Status     string `json:"status"`
}

func toOrderJSON(o store.Order) orderJSON {
	return orderJSON{ID: o.ID, ProductID: o.ProductID, Qty: o.Quantity, TotalCents: o.TotalCents, Status: o.Status}
}

// POST /orders — cria um pedido e baixa o estoque.
func (s *Server) createOrder(w http.ResponseWriter, r *http.Request) {
	var in orderInput
	if err := decode(r, &in); err != nil {
		writeError(w, err)
		return
	}
	if in.Qty <= 0 {
		writeError(w, errors.New("qty must be positive"))
		return
	}
	o, err := s.store.CreateOrder(in.ProductID, in.Qty)
	if err != nil {
		writeError(w, err)
		return
	}
	w.Header().Set("Location", "/orders/"+o.ID)
	writeJSON(w, http.StatusCreated, toOrderJSON(o))
}

// GET /orders/{id}
func (s *Server) getOrder(w http.ResponseWriter, r *http.Request) {
	o, err := s.store.GetOrder(r.PathValue("id"))
	if err != nil {
		writeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, toOrderJSON(o))
}
