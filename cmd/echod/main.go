// Command echod sobe o concentrador de leituras (servidor de eco de frames).
//
//	echod -addr :7070 -idle 2m -write-timeout 10s
//
// Encerra com SIGINT/SIGTERM chamando Server.Close.
package main

import (
	"context"
	"errors"
	"flag"
	"log/slog"
	"net"
	"os"
	"os/signal"
	"syscall"

	"desafio22/src/echo"
)

func main() {
	addr := flag.String("addr", ":7070", "endereço de escuta")
	idle := flag.Duration("idle", echo.DefaultIdleTimeout, "IdleTimeout das conexões")
	wt := flag.Duration("write-timeout", echo.DefaultWriteTimeout, "WriteTimeout das respostas")
	flag.Parse()

	log := slog.New(slog.NewJSONHandler(os.Stdout, nil))
	ln, err := net.Listen("tcp", *addr)
	if err != nil {
		log.Error("listen", "err", err)
		os.Exit(1)
	}
	srv := &echo.Server{IdleTimeout: *idle, WriteTimeout: *wt, Logger: log}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	closed := make(chan struct{})
	go func() {
		defer close(closed)
		<-ctx.Done()
		srv.Close()
	}()

	log.Info("listening", "addr", ln.Addr().String())
	if err := srv.Serve(ln); !errors.Is(err, echo.ErrServerClosed) {
		log.Error("serve", "err", err)
		os.Exit(1)
	}
	<-closed // Close devolve o controle quando as conexões terminaram
	log.Info("encerrado")
}
