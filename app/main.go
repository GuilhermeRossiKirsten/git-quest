// Command shop é a pequena loja usada no desafio-25. Um único binário faz o
// papel de todos os componentes; o primeiro argumento escolhe qual.
//
// Este código NÃO faz parte do que você deve alterar: o desafio está em quem
// pode fazer o quê no cluster (k8s/). Ele é versionado para que você entenda o
// que cada componente realmente precisa.
//
// Componentes (rodam como pods):
//
//	/app frontend      HTTP :8080. GET / consulta a api (API_URL) e mostra os pedidos.
//	/app api           HTTP :8080. GET /orders consulta o banco (DB_ADDR, DB_PASSWORD).
//	/app db            "banco" fake em TCP :5432, protocolo de texto de uma linha:
//	                   "AUTH <senha>", "QUERY orders", "PING".
//	/app maintenance   job de manutenção: lista os pods do namespace pela API do
//	                   Kubernetes e grava um resumo no ConfigMap REPORT_CONFIGMAP
//	                   (padrão maintenance-report). Usa o token da ServiceAccount.
//	/app idle          HTTP :8080 que só responde /healthz (pods auxiliares).
//
// Ferramentas de diagnóstico (a imagem não tem shell; use kubectl exec):
//
//	/app connect <host:porta>   abre uma conexão TCP (timeout de 3 s) e diz se
//	                            conectou, se o nome não resolveu (DNS), se a
//	                            conexão foi recusada ou se ficou sem resposta.
//	/app probe <url>            GET; imprime "HTTP <status>" + corpo; sai 0 só para 2xx.
//	/app tokencheck             diz se há um token de ServiceAccount montado no pod.
package main

import (
	"bufio"
	"bytes"
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"sort"
	"strings"
	"syscall"
	"time"
)

const (
	httpAddr = ":8080"
	dbAddr   = ":5432"
	saDir    = "/var/run/secrets/kubernetes.io/serviceaccount"
)

func main() {
	log.SetFlags(0)
	if len(os.Args) < 2 {
		usage()
	}
	arg := func(i int) string {
		if len(os.Args) <= i {
			usage()
		}
		return os.Args[i]
	}
	switch os.Args[1] {
	case "frontend":
		serveHTTP(frontendHandler())
	case "api":
		serveHTTP(apiHandler())
	case "db":
		serveDB()
	case "idle":
		serveHTTP(http.NewServeMux())
	case "maintenance":
		if err := runMaintenance(); err != nil {
			log.Printf("ERRO %v", err)
			os.Exit(1)
		}
	case "connect":
		os.Exit(connect(arg(2)))
	case "probe":
		os.Exit(probe(arg(2)))
	case "tokencheck":
		tokenCheck()
	default:
		usage()
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, "uso: /app frontend|api|db|maintenance|idle|connect <host:porta>|probe <url>|tokencheck")
	os.Exit(2)
}

func env(name, def string) string {
	if v := os.Getenv(name); v != "" {
		return v
	}
	return def
}

// ---------------------------------------------------------------------------
// Servidores

// serveHTTP atende em :8080 com /healthz e encerra limpo no SIGTERM.
func serveHTTP(mux *http.ServeMux) {
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		fmt.Fprintln(w, "ok")
	})
	srv := &http.Server{Addr: httpAddr, Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM, os.Interrupt)
	defer stop()
	go func() {
		<-ctx.Done()
		shutdown, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		_ = srv.Shutdown(shutdown)
	}()
	log.Printf("%s ouvindo em %s", os.Args[1], httpAddr)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatalf("ERRO %v", err)
	}
}

var client = &http.Client{Timeout: 4 * time.Second}

func frontendHandler() *http.ServeMux {
	apiURL := env("API_URL", "http://api:8080")
	title := env("SHOP_TITLE", "Loja")
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", func(w http.ResponseWriter, _ *http.Request) {
		resp, err := client.Get(apiURL + "/orders")
		if err != nil {
			http.Error(w, "api indisponível: "+err.Error(), http.StatusBadGateway)
			return
		}
		defer resp.Body.Close()
		body, _ := io.ReadAll(io.LimitReader(resp.Body, 1<<16))
		if resp.StatusCode != http.StatusOK {
			http.Error(w, fmt.Sprintf("api respondeu %d: %s", resp.StatusCode, bytes.TrimSpace(body)), http.StatusBadGateway)
			return
		}
		var out struct {
			Orders []order `json:"orders"`
		}
		if err := json.Unmarshal(body, &out); err != nil {
			http.Error(w, "resposta inválida da api", http.StatusBadGateway)
			return
		}
		fmt.Fprintf(w, "%s — %d pedido(s)\n", title, len(out.Orders))
		for _, o := range out.Orders {
			fmt.Fprintf(w, "  %s  %-24s %s\n", o.ID, o.Customer, o.Status)
		}
	})
	return mux
}

type order struct {
	ID       string `json:"id"`
	Customer string `json:"customer"`
	Status   string `json:"status"`
}

func apiHandler() *http.ServeMux {
	addr := env("DB_ADDR", "db:5432")
	password := os.Getenv("DB_PASSWORD")
	mux := http.NewServeMux()
	mux.HandleFunc("GET /orders", func(w http.ResponseWriter, _ *http.Request) {
		line, err := queryDB(addr, password, "QUERY orders")
		if err != nil {
			http.Error(w, "banco indisponível: "+err.Error(), http.StatusServiceUnavailable)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, `{"orders":%s}`+"\n", line)
	})
	return mux
}

// queryDB autentica no banco e executa um comando, devolvendo a resposta.
func queryDB(addr, password, cmd string) (string, error) {
	conn, err := net.DialTimeout("tcp", addr, 3*time.Second)
	if err != nil {
		return "", err
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(3 * time.Second))
	r := bufio.NewReader(conn)
	for _, c := range []string{"AUTH " + password, cmd} {
		if _, err := fmt.Fprintln(conn, c); err != nil {
			return "", err
		}
		reply, err := r.ReadString('\n')
		if err != nil {
			return "", err
		}
		reply = strings.TrimSpace(reply)
		if strings.HasPrefix(reply, "ERR") {
			return "", errors.New(reply)
		}
		if !strings.HasPrefix(c, "AUTH ") {
			return reply, nil
		}
	}
	return "", errors.New("resposta inesperada do banco")
}

var orders = []order{
	{ID: "PED-1001", Customer: "Ana Souza", Status: "enviado"},
	{ID: "PED-1002", Customer: "Bruno Lima", Status: "pago"},
	{ID: "PED-1003", Customer: "Carla Dias", Status: "aguardando"},
}

func serveDB() {
	password := os.Getenv("DB_PASSWORD")
	if password == "" {
		log.Fatal("ERRO DB_PASSWORD não definida")
	}
	ln, err := net.Listen("tcp", dbAddr)
	if err != nil {
		log.Fatalf("ERRO %v", err)
	}
	go func() {
		sig := make(chan os.Signal, 1)
		signal.Notify(sig, syscall.SIGTERM, os.Interrupt)
		<-sig
		_ = ln.Close()
	}()
	log.Printf("db ouvindo em %s", dbAddr)
	data, _ := json.Marshal(orders)
	for {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		go handleDBConn(conn, password, string(data))
	}
}

func handleDBConn(conn net.Conn, password, data string) {
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(10 * time.Second))
	sc := bufio.NewScanner(conn)
	authed := false
	for sc.Scan() {
		cmd := strings.TrimSpace(sc.Text())
		switch {
		case cmd == "PING":
			fmt.Fprintln(conn, "PONG")
		case strings.HasPrefix(cmd, "AUTH "):
			authed = strings.TrimPrefix(cmd, "AUTH ") == password
			if !authed {
				fmt.Fprintln(conn, "ERR senha inválida")
				return
			}
			fmt.Fprintln(conn, "OK")
		case cmd == "QUERY orders" && authed:
			fmt.Fprintln(conn, data)
		case !authed:
			fmt.Fprintln(conn, "ERR autentique-se primeiro")
			return
		default:
			fmt.Fprintln(conn, "ERR comando desconhecido")
		}
	}
}

// ---------------------------------------------------------------------------
// Job de manutenção: cliente mínimo da API do Kubernetes (só biblioteca padrão).

type kubeClient struct {
	base  string
	token string
	http  *http.Client
}

func inCluster() (*kubeClient, string, error) {
	token, err := os.ReadFile(saDir + "/token")
	if err != nil {
		return nil, "", fmt.Errorf("sem token de ServiceAccount em %s (o job precisa falar com a API do Kubernetes): %w", saDir, err)
	}
	ns, err := os.ReadFile(saDir + "/namespace")
	if err != nil {
		return nil, "", err
	}
	ca, err := os.ReadFile(saDir + "/ca.crt")
	if err != nil {
		return nil, "", err
	}
	pool := x509.NewCertPool()
	pool.AppendCertsFromPEM(ca)
	host, port := os.Getenv("KUBERNETES_SERVICE_HOST"), os.Getenv("KUBERNETES_SERVICE_PORT")
	if host == "" || port == "" {
		return nil, "", errors.New("KUBERNETES_SERVICE_HOST/PORT não definidos: não estou num pod")
	}
	return &kubeClient{
		base:  "https://" + net.JoinHostPort(host, port),
		token: strings.TrimSpace(string(token)),
		http: &http.Client{Timeout: 10 * time.Second, Transport: &http.Transport{
			TLSClientConfig: &tls.Config{RootCAs: pool, MinVersion: tls.VersionTLS12},
		}},
	}, strings.TrimSpace(string(ns)), nil
}

type apiError struct {
	verb, path string
	code       int
	message    string
}

func (e *apiError) Error() string {
	return fmt.Sprintf("%s %s: HTTP %d: %s", e.verb, e.path, e.code, e.message)
}

func (k *kubeClient) do(verb, path string, in, out any) error {
	var body io.Reader
	if in != nil {
		b, err := json.Marshal(in)
		if err != nil {
			return err
		}
		body = bytes.NewReader(b)
	}
	req, err := http.NewRequest(verb, k.base+path, body)
	if err != nil {
		return err
	}
	req.Header.Set("Authorization", "Bearer "+k.token)
	req.Header.Set("Accept", "application/json")
	if in != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := k.http.Do(req)
	if err != nil {
		return fmt.Errorf("%s %s: %w", verb, path, err)
	}
	defer resp.Body.Close()
	raw, _ := io.ReadAll(io.LimitReader(resp.Body, 4<<20))
	if resp.StatusCode/100 != 2 {
		var st struct {
			Message string `json:"message"`
		}
		_ = json.Unmarshal(raw, &st)
		return &apiError{verb, path, resp.StatusCode, st.Message}
	}
	if out != nil {
		return json.Unmarshal(raw, out)
	}
	return nil
}

type configMap struct {
	APIVersion string            `json:"apiVersion"`
	Kind       string            `json:"kind"`
	Metadata   map[string]any    `json:"metadata"`
	Data       map[string]string `json:"data"`
}

func runMaintenance() error {
	k, ns, err := inCluster()
	if err != nil {
		return err
	}
	name := env("REPORT_CONFIGMAP", "maintenance-report")

	// 1. Quais pods estão de pé? (GET .../pods = verbo "list" no RBAC)
	var pods struct {
		Items []struct {
			Metadata struct {
				Labels map[string]string `json:"labels"`
			} `json:"metadata"`
			Status struct {
				Phase      string `json:"phase"`
				Conditions []struct {
					Type, Status string
				} `json:"conditions"`
			} `json:"status"`
		} `json:"items"`
	}
	if err := k.do("GET", "/api/v1/namespaces/"+ns+"/pods", nil, &pods); err != nil {
		return err
	}
	ready, total := map[string]int{}, map[string]int{}
	for _, p := range pods.Items {
		app := p.Metadata.Labels["app.kubernetes.io/name"]
		if app == "" || p.Status.Phase == "Succeeded" || p.Status.Phase == "Failed" {
			continue
		}
		total[app]++
		for _, c := range p.Status.Conditions {
			if c.Type == "Ready" && c.Status == "True" {
				ready[app]++
			}
		}
	}
	apps := make([]string, 0, len(total))
	for a := range total {
		apps = append(apps, a)
	}
	sort.Strings(apps)
	summary := make([]string, 0, len(apps))
	for _, a := range apps {
		summary = append(summary, fmt.Sprintf("%s=%d/%d", a, ready[a], total[a]))
	}
	data := map[string]string{
		"lastRunBy": env("HOSTNAME", "desconhecido"),
		"lastRunAt": time.Now().UTC().Format(time.RFC3339),
		"pods":      strings.Join(summary, ","),
	}

	// 2. Grava o relatório: GET + PUT (verbos "get" e "update"); cria se não existir.
	path := "/api/v1/namespaces/" + ns + "/configmaps/" + name
	for attempt := 1; ; attempt++ {
		var cm configMap
		err := k.do("GET", path, nil, &cm)
		var ae *apiError
		switch {
		case errors.As(err, &ae) && ae.code == http.StatusNotFound:
			cm = configMap{APIVersion: "v1", Kind: "ConfigMap",
				Metadata: map[string]any{"name": name}, Data: data}
			err = k.do("POST", "/api/v1/namespaces/"+ns+"/configmaps", cm, nil)
		case err == nil:
			cm.Data = data
			err = k.do("PUT", path, cm, nil)
		}
		if errors.As(err, &ae) && ae.code == http.StatusConflict && attempt < 3 {
			continue
		}
		if err != nil {
			return err
		}
		break
	}
	log.Printf("relatório %s atualizado: %s", name, data["pods"])
	return nil
}

// ---------------------------------------------------------------------------
// Diagnóstico

// connect sai com 0 (conectou), 2 (DNS), 3 (sem resposta), 4 (recusada) ou 1.
func connect(addr string) int {
	host, _, err := net.SplitHostPort(addr)
	if err != nil {
		fmt.Println("ERRO endereço inválido (use host:porta):", err)
		return 1
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if net.ParseIP(host) == nil {
		if _, err := net.DefaultResolver.LookupHost(ctx, host); err != nil {
			fmt.Printf("ERRO dns: não foi possível resolver %q (%v)\n", host, err)
			return 2
		}
	}
	conn, err := (&net.Dialer{}).DialContext(ctx, "tcp", addr)
	if err == nil {
		conn.Close()
		fmt.Println("OK conectado a", addr)
		return 0
	}
	switch {
	case errors.Is(err, syscall.ECONNREFUSED):
		fmt.Println("ERRO recusada:", addr, "(nada ouvindo nessa porta?)")
		return 4
	case errors.Is(err, context.DeadlineExceeded) || os.IsTimeout(err):
		fmt.Println("ERRO sem resposta:", addr, "(pacotes descartados em 3s)")
		return 3
	}
	fmt.Println("ERRO", err)
	return 1
}

func probe(url string) int {
	resp, err := client.Get(url)
	if err != nil {
		fmt.Println("ERRO", err)
		return 1
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(resp.Body, 1<<16))
	fmt.Printf("HTTP %d\n%s", resp.StatusCode, body)
	if resp.StatusCode/100 != 2 {
		return 1
	}
	return 0
}

func tokenCheck() {
	if _, err := os.Stat(saDir + "/token"); err == nil {
		fmt.Println("token: presente em", saDir+"/token")
		return
	}
	fmt.Println("token: ausente")
}
