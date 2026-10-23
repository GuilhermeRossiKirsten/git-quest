package tests

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"net/http"
	"strings"
	"testing"

	"desafio34/internal/store"
)

// legacyClient reproduz o ERP do distribuidor parceiro: o "cliente v1",
// escrito por uma consultoria em 2023 contra o contrato v1 e que NINGUÉM pode
// mais alterar. Ele é um leitor tolerante (ignora campos que não conhece),
// mas depende de cada campo que lê.
type legacyClient struct {
	base   string
	hc     *http.Client
	keySeq int
}

type v1Product struct {
	ID    string  `json:"id"`
	Name  string  `json:"name"`
	Price float64 `json:"price"` // reais
	Stock int     `json:"stock"`
}

type v1Page struct {
	Items []v1Product `json:"items"`
	Next  *string     `json:"next"`
}

type v1Order struct {
	ID       string  `json:"id"`
	Quantity int     `json:"quantity"`
	Total    float64 `json:"total"` // reais
	Status   string  `json:"status"`
}

func newLegacyClient(e *env, rt http.RoundTripper) *legacyClient {
	hc := *e.hc
	if rt != nil {
		hc.Transport = rt
	}
	return &legacyClient{base: e.srv.URL, hc: &hc}
}

// SyncCatalog baixa o catálogo inteiro, página por página, seguindo "next".
func (c *legacyClient) SyncCatalog(pageSize int) ([]v1Product, error) {
	var all []v1Product
	next := fmt.Sprintf("/products?limit=%d", pageSize)
	for pages := 1; ; pages++ {
		if pages > 50 {
			return nil, errors.New("mais de 50 páginas: o link next nunca termina")
		}
		res, err := c.hc.Get(c.base + next)
		if err != nil {
			return nil, err
		}
		data, _ := io.ReadAll(res.Body)
		res.Body.Close()
		if res.StatusCode != 200 {
			return nil, fmt.Errorf("GET %s → %d: %s", next, res.StatusCode, data)
		}
		var page v1Page
		if err := json.Unmarshal(data, &page); err != nil {
			return nil, fmt.Errorf("GET %s: resposta ilegível: %v", next, err)
		}
		for _, p := range page.Items {
			if p.ID == "" || p.Price <= 0 {
				return nil, fmt.Errorf("produto %q sem preço (price=%v) — o ERP recusa o lote inteiro; JSON recebido: %s",
					p.ID, p.Price, strings.TrimSpace(string(data)))
			}
		}
		all = append(all, page.Items...)
		if page.Next == nil {
			return all, nil
		}
		next = *page.Next
	}
}

// PlaceOrder cria um pedido. Desde o primeiro dia o ERP manda uma
// Idempotency-Key por pedido e, em erro de rede ou 5xx, tenta de novo (até 3
// vezes) com a MESMA chave — como o contrato v1 sempre disse que podia.
func (c *legacyClient) PlaceOrder(productID string, quantity int) (v1Order, error) {
	c.keySeq++
	key := fmt.Sprintf("erp-%04d", c.keySeq)
	body, _ := json.Marshal(map[string]any{"product_id": productID, "quantity": quantity})
	var lastErr error
	for attempt := 1; attempt <= 3; attempt++ {
		req, _ := http.NewRequest("POST", c.base+"/orders", bytes.NewReader(body))
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Idempotency-Key", key)
		res, err := c.hc.Do(req)
		if err != nil {
			lastErr = fmt.Errorf("tentativa %d: %v", attempt, err)
			continue
		}
		data, _ := io.ReadAll(res.Body)
		res.Body.Close()
		if res.StatusCode >= 500 {
			lastErr = fmt.Errorf("tentativa %d: %d %s", attempt, res.StatusCode, strings.TrimSpace(string(data)))
			continue
		}
		if res.StatusCode != 201 {
			return v1Order{}, fmt.Errorf("pedido recusado: %d %s", res.StatusCode, strings.TrimSpace(string(data)))
		}
		var o v1Order
		if err := json.Unmarshal(data, &o); err != nil || o.ID == "" || o.Status != "confirmed" {
			return v1Order{}, fmt.Errorf("confirmação ilegível: %s", data)
		}
		if o.Quantity != quantity || o.Total <= 0 {
			return v1Order{}, fmt.Errorf("confirmação sem quantity/total (quantity=%d, total=%v): %s", o.Quantity, o.Total, data)
		}
		return o, nil
	}
	return v1Order{}, fmt.Errorf("desistiu depois de 3 tentativas: %v", lastErr)
}

// TestCompat_ClienteV1: o ERP do parceiro sincroniza o catálogo e faz um
// pedido; o app novo continua vendo os campos novos.
func TestCompat_ClienteV1(t *testing.T) {
	e := newEnv(t, store.Hooks{})
	cents := []int64{1990, 250, 100000, 5, 1234, 999, 4500}
	seeded := e.seed(cents...)
	erp := newLegacyClient(e, nil)

	got, err := erp.SyncCatalog(3)
	if err != nil {
		t.Fatalf("cliente v1 não conseguiu sincronizar o catálogo: %v", err)
	}
	byID := map[string]v1Product{}
	for _, p := range got {
		byID[p.ID] = p
	}
	if len(got) != len(seeded) || len(byID) != len(seeded) {
		t.Errorf("cliente v1 recebeu %d produtos (%d distintos), esperado %d", len(got), len(byID), len(seeded))
	}
	for _, sp := range seeded {
		p, ok := byID[sp.ID]
		want := float64(sp.PriceCents) / 100
		switch {
		case !ok:
			t.Errorf("produto %s não chegou ao cliente v1", sp.ID)
		case math.Abs(p.Price-want) > 1e-9:
			t.Errorf("produto %s: price = %v, esperado %v (reais)", sp.ID, p.Price, want)
		case p.Stock != sp.Stock || p.Name != sp.Name:
			t.Errorf("produto %s: name/stock = %q/%d, esperado %q/%d", sp.ID, p.Name, p.Stock, sp.Name, sp.Stock)
		}
	}

	o, err := erp.PlaceOrder(seeded[0].ID, 2)
	if err != nil {
		t.Errorf("cliente v1 não conseguiu fazer um pedido: %v", err)
	} else if math.Abs(o.Total-39.80) > 1e-9 {
		t.Errorf("pedido do cliente v1: total = %v, esperado 39.8 (2 × 19,90)", o.Total)
	}
	if p, _ := e.store.GetProduct(seeded[0].ID); err == nil && p.Stock != 48 {
		t.Errorf("estoque depois do pedido do cliente v1 = %d, esperado 48", p.Stock)
	}

	// O app novo depende dos campos novos: eles também precisam estar lá.
	r := e.do("GET", "/products/"+seeded[2].ID, nil)
	if p := object(t, r); p["price_cents"] != 100000.0 {
		t.Errorf("o app novo lê price_cents: esperado 100000\n        %s", r)
	}
	r = e.do("POST", "/orders", map[string]any{"product_id": seeded[1].ID, "quantity": 3})
	if r.Status != 201 {
		t.Fatalf("POST /orders com quantity: esperado 201\n        %s", r)
	}
	if o := object(t, r); o["total_cents"] != 750.0 || o["total"] != 7.5 || o["quantity"] != 3.0 {
		t.Errorf("pedido: esperado total_cents 750 (app novo), total 7.5 e quantity 3 (cliente v1)\n        %s", r)
	}
}

// dropFirstOrderResponse entrega o primeiro POST /orders ao servidor, mas
// "perde" a resposta no caminho de volta — como um timeout no balanceador.
type dropFirstOrderResponse struct {
	base    http.RoundTripper
	dropped bool
}

func (d *dropFirstOrderResponse) RoundTrip(req *http.Request) (*http.Response, error) {
	res, err := d.base.RoundTrip(req)
	if err == nil && !d.dropped && req.Method == "POST" && req.URL.Path == "/orders" {
		d.dropped = true
		io.Copy(io.Discard, res.Body)
		res.Body.Close()
		return nil, errors.New("read: connection reset by peer (simulado: a resposta se perdeu na rede)")
	}
	return res, err
}

// TestCompat_RetentativaV1: a resposta do primeiro POST se perde e o ERP tenta
// de novo com a mesma Idempotency-Key — o cliente final só pode ser cobrado
// uma vez.
func TestCompat_RetentativaV1(t *testing.T) {
	e := newEnv(t, store.Hooks{})
	p := e.store.CreateProduct(store.Product{Name: "Furadeira", PriceCents: 25000, Stock: 10})
	erp := newLegacyClient(e, &dropFirstOrderResponse{base: e.hc.Transport})

	o, err := erp.PlaceOrder(p.ID, 3)
	if err != nil {
		t.Errorf("cliente v1 (com a resposta do 1º POST perdida) não conseguiu fazer o pedido: %v", err)
	}
	orders := e.store.Orders()
	if len(orders) != 1 {
		t.Errorf("a retentativa criou %d pedidos para a mesma Idempotency-Key, esperado exatamente 1", len(orders))
	} else if err == nil && orders[0].ID != o.ID {
		t.Errorf("a retentativa devolveu o pedido %s, mas o pedido gravado é %s", o.ID, orders[0].ID)
	}
	if cur, _ := e.store.GetProduct(p.ID); cur.Stock != 7 {
		t.Errorf("estoque = %d, esperado 7 (baixa de 3 unidades uma única vez)", cur.Stock)
	}
}
