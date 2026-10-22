// Package echo implementa o concentrador de leituras das balanças: um
// servidor TCP que recebe frames (pacote frame) e devolve cada um, intacto,
// ao mesmo cliente — o eco é a confirmação de recebimento que o firmware
// da balança espera antes de apagar a leitura da memória local.
package echo

import (
	"errors"
	"io"
	"log/slog"
	"net"
	"sync"
	"time"

	"desafio22/src/frame"
)

// Valores usados quando os campos correspondentes de Server são zero.
const (
	DefaultIdleTimeout  = 2 * time.Minute
	DefaultWriteTimeout = 10 * time.Second
)

// ErrServerClosed é devolvido por Serve depois de Close.
var ErrServerClosed = errors.New("echo: servidor fechado")

// Server é o servidor de eco. O valor zero está pronto para uso.
type Server struct {
	// IdleTimeout é quanto tempo uma conexão pode ficar sem entregar um
	// frame completo antes de ser derrubada.
	IdleTimeout time.Duration
	// WriteTimeout é o prazo para entregar cada resposta ao cliente.
	WriteTimeout time.Duration
	// Logger recebe os eventos do servidor (nil = slog.Default()).
	Logger *slog.Logger

	mu     sync.Mutex
	ln     net.Listener
	closed bool
}

// Serve aceita conexões em ln até Close ser chamado; então devolve
// ErrServerClosed.
func (s *Server) Serve(ln net.Listener) error {
	s.mu.Lock()
	s.ln = ln
	s.mu.Unlock()

	for {
		conn, err := ln.Accept()
		if err != nil {
			s.mu.Lock()
			closed := s.closed
			s.mu.Unlock()
			if closed {
				return ErrServerClosed
			}
			return err
		}
		go s.handle(conn)
	}
}

// Close para de aceitar conexões.
func (s *Server) Close() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.closed = true
	if s.ln == nil {
		return nil
	}
	return s.ln.Close()
}

func (s *Server) handle(conn net.Conn) {
	defer conn.Close()
	log := s.logger().With("remote", conn.RemoteAddr().String())

	// Derruba clientes pendurados.
	conn.SetDeadline(time.Now().Add(s.idleTimeout()))

	for {
		msg, err := frame.ReadFrame(conn)
		if err != nil {
			if err != io.EOF {
				log.Warn("leitura falhou", "err", err)
			}
			return
		}
		// Responde em paralelo para não atrasar a leitura do próximo frame.
		go func() {
			conn.SetWriteDeadline(time.Now().Add(s.writeTimeout()))
			if err := frame.WriteFrame(conn, msg); err != nil {
				log.Warn("escrita falhou", "err", err)
			}
		}()
	}
}

func (s *Server) logger() *slog.Logger {
	if s.Logger != nil {
		return s.Logger
	}
	return slog.Default()
}

func (s *Server) idleTimeout() time.Duration {
	if s.IdleTimeout > 0 {
		return s.IdleTimeout
	}
	return DefaultIdleTimeout
}

func (s *Server) writeTimeout() time.Duration {
	if s.WriteTimeout > 0 {
		return s.WriteTimeout
	}
	return DefaultWriteTimeout
}
