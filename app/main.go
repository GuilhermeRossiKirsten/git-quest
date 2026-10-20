// greeter — gitquest-desafio-9-source
//
// Microsserviço HTTP mínimo (somente biblioteca padrão) usado no desafio-9.
// Este arquivo NÃO faz parte do que você deve alterar: o trabalho está no
// Dockerfile e no .dockerignore.
//
// Uso:
//
//	greeter              inicia o servidor HTTP em :8080 (o mesmo que "serve")
//	greeter serve        idem
//	greeter healthcheck  consulta http://127.0.0.1:8080/healthz e sai com 0 (saudável) ou 1
//
// O subcomando healthcheck existe para que a imagem possa declarar um
// HEALTHCHECK mesmo sem shell nem curl dentro dela.
package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"
)

const (
	listenAddr = ":8080"
	healthURL  = "http://127.0.0.1:8080/healthz"
)

func main() {
	cmd := "serve"
	if len(os.Args) > 1 {
		cmd = os.Args[1]
	}
	switch cmd {
	case "serve":
		if err := serve(); err != nil {
			log.Fatalf("greeter: %v", err)
		}
	case "healthcheck":
		os.Exit(healthcheck(healthURL))
	default:
		fmt.Fprintf(os.Stderr, "uso: %s [serve|healthcheck]\n", os.Args[0])
		os.Exit(2)
	}
}

// serve roda o servidor até receber SIGINT/SIGTERM e então encerra com elegância.
// O serviço não escreve em disco: funciona com o sistema de arquivos somente leitura.
func serve() error {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	srv := &http.Server{
		Addr:              listenAddr,
		Handler:           newMux(greetingFromEnv()),
		ReadHeaderTimeout: 5 * time.Second,
	}

	errc := make(chan error, 1)
	go func() {
		log.Printf("greeter ouvindo em %s (uid=%d)", listenAddr, os.Getuid())
		errc <- srv.ListenAndServe()
	}()

	select {
	case err := <-errc:
		return err
	case <-ctx.Done():
	}
	log.Print("greeter: encerrando")
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := srv.Shutdown(shutdownCtx); err != nil && !errors.Is(err, http.ErrServerClosed) {
		return err
	}
	return nil
}

// healthcheck devolve o código de saída esperado por HEALTHCHECK: 0 = saudável.
func healthcheck(url string) int {
	client := &http.Client{Timeout: 2 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		fmt.Fprintf(os.Stderr, "healthcheck: %v\n", err)
		return 1
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		fmt.Fprintf(os.Stderr, "healthcheck: status %d\n", resp.StatusCode)
		return 1
	}
	return 0
}
