// Command demo sobe a API da loja (src/api) em memória e reproduz os chamados
// abertos pelo parceiro e pelo time do app — um por um, comparando o que
// acontece com o que o contrato promete.
//
//	make demo        (dentro do container — ./start.sh já executa uma vez)
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"time"

	"desafio34/internal/store"
	"desafio34/src/api"
)

func main() {
	scenario("1. ERP do parceiro (cliente v1) sincroniza o catálogo",
		`cada produto com "price" em reais (ex.: 19.9)`, func() (bool, string) {
			st, base := start()
			p := st.CreateProduct(store.Product{Name: "Martelo", PriceCents: 1990, Stock: 5})
			r := call("GET", base+"/products/"+p.ID, nil)
			var v1 struct {
				Price float64 `json:"price"`
			}
			json.Unmarshal(r.body, &v1)
			return v1.Price == 19.9, fmt.Sprintf("price=%v — corpo: %s", v1.Price, bytes.TrimSpace(r.body))
		})

	scenario("2. ERP faz um pedido como sempre fez (\"quantity\": 2)",
		"201 com quantity 2 e total 39.8", func() (bool, string) {
			st, base := start()
			p := st.CreateProduct(store.Product{Name: "Martelo", PriceCents: 1990, Stock: 5})
			r := call("POST", base+"/orders", map[string]any{"product_id": p.ID, "quantity": 2})
			return r.status == 201 && strings.Contains(string(r.body), `"total":39.8`), r.String()
		})

	scenario("3. Catálogo paginado enquanto a loja cadastra produtos novos",
		"a 2ª página continua de onde a 1ª parou, sem repetir nada", func() (bool, string) {
			st, base := start()
			for i := range 6 {
				st.CreateProduct(store.Product{Name: fmt.Sprintf("P%d", i+1), PriceCents: 100, Stock: 1})
			}
			first := ids(call("GET", base+"/products?limit=3", nil))
			st.CreateProduct(store.Product{Name: "Novo", PriceCents: 100, Stock: 1})
			st.CreateProduct(store.Product{Name: "Novo", PriceCents: 100, Stock: 1})
			second := ids(call("GET", base+first.next, nil))
			dup := 0
			for _, a := range second.ids {
				for _, b := range first.ids {
					if a == b {
						dup++
					}
				}
			}
			return dup == 0, fmt.Sprintf("página 1 %v, página 2 %v → %d repetido(s)", first.ids, second.ids, dup)
		})

	scenario("4. Ana muda o preço e Beto o estoque do mesmo produto, quase juntos",
		"a 2ª gravação, feita sobre uma leitura velha, é recusada (412)", func() (bool, string) {
			st, base := start()
			p := st.CreateProduct(store.Product{Name: "Cadeira", PriceCents: 50000, Stock: 10})
			tag := call("GET", base+"/products/"+p.ID, nil).header.Get("ETag")
			hdr := []string{"If-Match", tag}
			call("PUT", base+"/products/"+p.ID, map[string]any{"name": "Cadeira", "price_cents": 45000, "stock": 10}, hdr...)
			r := call("PUT", base+"/products/"+p.ID, map[string]any{"name": "Cadeira", "price_cents": 50000, "stock": 3}, hdr...)
			cur, _ := st.GetProduct(p.ID)
			return r.status == 412, fmt.Sprintf("2º PUT → %d; preço final %d (a Ana tinha posto 45000)", r.status, cur.PriceCents)
		})

	scenario("5. Resposta do POST /orders se perde e o ERP reenvia com a mesma Idempotency-Key",
		"um único pedido", func() (bool, string) {
			st, base := start()
			p := st.CreateProduct(store.Product{Name: "Furadeira", PriceCents: 25000, Stock: 10})
			body := map[string]any{"product_id": p.ID, "quantity": 1, "qty": 1}
			call("POST", base+"/orders", body, "Idempotency-Key", "erp-0001")
			call("POST", base+"/orders", body, "Idempotency-Key", "erp-0001")
			n := len(st.Orders())
			return n == 1, fmt.Sprintf("%d pedido(s) gravado(s) — o cliente foi cobrado %d vez(es)", n, n)
		})

	scenario("6. App consulta um pedido que não existe",
		"404 application/problem+json", func() (bool, string) {
			_, base := start()
			r := call("GET", base+"/orders/ord_9999", nil)
			return r.status == 404 && r.header.Get("Content-Type") == "application/problem+json", r.String()
		})

	scenario("7. App reabre a tela do produto, que não mudou (If-None-Match)",
		"304 Not Modified, sem corpo", func() (bool, string) {
			st, base := start()
			p := st.CreateProduct(store.Product{Name: "Luminária", PriceCents: 12900, Stock: 8})
			tag := call("GET", base+"/products/"+p.ID, nil).header.Get("ETag")
			if tag == "" {
				return false, "a resposta não tem ETag: o app não tem o que mandar em If-None-Match"
			}
			r := call("GET", base+"/products/"+p.ID, nil, "If-None-Match", tag)
			return r.status == 304, r.String()
		})
}

func scenario(title, want string, run func() (bool, string)) {
	fmt.Printf("\n%s\n   esperado: %s\n", title, want)
	done := make(chan [2]string, 1)
	go func() {
		defer func() {
			if v := recover(); v != nil {
				done <- [2]string{"✘", fmt.Sprintf("pânico: %v", v)}
			}
		}()
		ok, got := run()
		mark := "✔"
		if !ok {
			mark = "✘"
		}
		done <- [2]string{mark, got}
	}()
	select {
	case r := <-done:
		fmt.Printf("   obtido:   %s %s\n", r[0], r[1])
	case <-time.After(10 * time.Second):
		fmt.Printf("   obtido:   ✘ travou por 10s\n")
	}
}

func start() (*store.Store, string) {
	st := store.New()
	srv := httptest.NewServer(api.New(st)) // vive até o fim do processo
	return st, srv.URL
}

type result struct {
	status int
	header http.Header
	body   []byte
}

func (r result) String() string {
	return fmt.Sprintf("%d %s %s", r.status, r.header.Get("Content-Type"), strings.TrimSpace(string(r.body)))
}

func call(method, url string, body any, hdr ...string) result {
	var rd io.Reader
	if body != nil {
		raw, _ := json.Marshal(body)
		rd = bytes.NewReader(raw)
	}
	req, _ := http.NewRequest(method, url, rd)
	req.Header.Set("Content-Type", "application/json")
	for i := 0; i+1 < len(hdr); i += 2 {
		req.Header.Set(hdr[i], hdr[i+1])
	}
	res, err := http.DefaultClient.Do(req)
	if err != nil {
		return result{header: http.Header{}, body: []byte(err.Error())}
	}
	defer res.Body.Close()
	data, _ := io.ReadAll(res.Body)
	return result{status: res.StatusCode, header: res.Header, body: data}
}

type pageIDs struct {
	ids  []string
	next string
}

func ids(r result) pageIDs {
	var page struct {
		Items []struct {
			ID string `json:"id"`
		} `json:"items"`
		Next *string `json:"next"`
	}
	json.Unmarshal(r.body, &page)
	var out pageIDs
	for _, it := range page.Items {
		out.ids = append(out.ids, it.ID)
	}
	if page.Next != nil {
		out.next = *page.Next
	}
	return out
}
