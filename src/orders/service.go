// Package orders implementa o recebimento de pedidos e o journal em arquivo.
package orders

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"net/http"
	"sync/atomic"
	"time"

	"desafio17/src/httpx"
)

// Item é uma linha do pedido.
type Item struct {
	SKU        string `json:"sku"`
	Qty        int    `json:"qty"`
	PriceCents int64  `json:"price_cents"`
}

// Order é o corpo de POST /orders.
type Order struct {
	Customer string `json:"customer"`
	Items    []Item `json:"items"`
}

// Created é a resposta de um pedido aceito (201).
type Created struct {
	OrderID   string `json:"order_id"`
	RequestID string `json:"request_id"`
}

// Service atende POST /orders: valida o pedido, executa o trabalho lento
// (reserva de estoque + cobrança, simulados por WorkDelay) e grava o pedido
// no journal. Um pedido só "existe" depois de gravado no journal.
type Service struct {
	Journal   *Journal
	WorkDelay time.Duration
	Logger    *slog.Logger

	seq atomic.Int64
}

// CreateOrder é o handler de POST /orders.
func (s *Service) CreateOrder(w http.ResponseWriter, r *http.Request) {
	var o Order
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<20)).Decode(&o); err != nil || o.Customer == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "pedido inválido"})
		return
	}

	var total int64
	units := 0
	for _, it := range o.Items {
		total += int64(it.Qty) * it.PriceCents
		units += it.Qty
	}

	// Reserva de estoque e cobrança no gateway de pagamento (simulado).
	time.Sleep(s.WorkDelay)

	rec := Record{
		OrderID:      fmt.Sprintf("ord-%06d", s.seq.Add(1)),
		RequestID:    httpx.RequestIDFrom(r.Context()),
		Customer:     o.Customer,
		Items:        o.Items,
		TotalCents:   total,
		AvgItemCents: total / int64(units),
		At:           time.Now().UTC(),
	}
	if err := s.Journal.Append(rec); err != nil {
		s.Logger.ErrorContext(r.Context(), "journal", "err", err, "order_id", rec.OrderID)
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "falha ao gravar o pedido"})
		return
	}
	writeJSON(w, http.StatusCreated, Created{OrderID: rec.OrderID, RequestID: rec.RequestID})
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}
