package tests

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"math/rand/v2"
	"net"
	"regexp"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"

	"desafio22/src/echo"
)

// Prefixo do pacote do servidor, para reconhecer suas goroutines em dumps.
const echoPkg = "desafio22/src/echo."

// maxFrame espelha frame.MaxSize do contrato (1 MiB), sem depender do código
// avaliado: os testes usam um codificador/decodificador de referência próprio.
const maxFrame = 1 << 20

// encode monta um frame conforme o contrato: uint32 big-endian + payload.
func encode(p []byte) []byte {
	b := make([]byte, 4+len(p))
	binary.BigEndian.PutUint32(b, uint32(len(p)))
	copy(b[4:], p)
	return b
}

// decode lê um frame de r com a implementação de referência.
func decode(r io.Reader) ([]byte, error) {
	var hdr [4]byte
	if _, err := io.ReadFull(r, hdr[:]); err != nil {
		return nil, err
	}
	n := binary.BigEndian.Uint32(hdr[:])
	if n > maxFrame {
		return nil, fmt.Errorf("frame corrompido: cabeçalho anuncia %d bytes (0x%08x)", n, n)
	}
	p := make([]byte, n)
	if _, err := io.ReadFull(r, p); err != nil {
		if err == io.EOF {
			err = io.ErrUnexpectedEOF
		}
		return nil, err
	}
	return p, nil
}

// payload gera n bytes determinísticos que identificam a mensagem seq.
// Todos os bytes ficam em 0..3: se o código avaliado perder o alinhamento dos
// frames, os "cabeçalhos" lidos do meio do payload anunciam no máximo ~50 MB,
// em vez de derrubar a suíte inteira pedindo 4 GiB.
func payload(seq, n int) []byte {
	p := make([]byte, n)
	r := rand.New(rand.NewPCG(uint64(seq), uint64(n)))
	for i := range p {
		p[i] = byte(r.IntN(4))
	}
	if n >= 8 { // seq em 8 dígitos de base 4 no início do payload
		for i, v := 7, seq; i >= 0; i, v = i-1, v/4 {
			p[i] = byte(v % 4)
		}
	}
	return p
}

// seqOf recupera o número de sequência gravado por payload.
func seqOf(p []byte) int {
	v := 0
	for _, d := range p[:8] {
		v = v*4 + int(d&3)
	}
	return v
}

// mismatch explica a diferença entre o payload esperado e o recebido.
func mismatch(want, got []byte) string {
	if len(want) == len(got) && (len(want) < 8 || bytes.Equal(want[:8], got[:8])) {
		i := 0
		for i < len(want) && want[i] == got[i] {
			i++
		}
		return fmt.Sprintf("esperado %s, recebido o mesmo tamanho mas com conteúdo diferente a partir do byte %d", describe(want), i)
	}
	return fmt.Sprintf("esperado %s, recebido %s", describe(want), describe(got))
}

// describe resume um payload recebido para mensagens de erro.
func describe(p []byte) string {
	if len(p) >= 8 {
		return fmt.Sprintf("%d bytes, seq=%d", len(p), seqOf(p))
	}
	return fmt.Sprintf("%d bytes", len(p))
}

var discard = slog.New(slog.DiscardHandler)

// smallBufListener reduz os buffers de socket das conexões aceitas, para que
// "o kernel guardou" não mascare "o servidor acumulou na memória".
type smallBufListener struct{ net.Listener }

func (l smallBufListener) Accept() (net.Conn, error) {
	c, err := l.Listener.Accept()
	if tc, ok := c.(*net.TCPConn); ok {
		tc.SetReadBuffer(32 << 10)
		tc.SetWriteBuffer(32 << 10)
	}
	return c, err
}

// testServer é um echo.Server rodando num listener de loopback.
type testServer struct {
	t        *testing.T
	srv      *echo.Server
	addr     string
	serveErr chan error
}

// startServer sobe srv em 127.0.0.1:0 (buffers de socket pequenos) e agenda
// Close no fim do teste.
func startServer(t *testing.T, srv *echo.Server) *testServer {
	t.Helper()
	if srv.Logger == nil {
		srv.Logger = discard
	}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	ts := &testServer{t: t, srv: srv, addr: ln.Addr().String(), serveErr: make(chan error, 1)}
	go func() { ts.serveErr <- srv.Serve(smallBufListener{ln}) }()
	t.Cleanup(func() { closeWithin(srv, 5*time.Second) })
	return ts
}

// closeWithin chama Close, mas não deixa um Close travado travar a suíte.
func closeWithin(srv *echo.Server, d time.Duration) (returned bool, err error) {
	done := make(chan error, 1)
	go func() { done <- srv.Close() }()
	select {
	case err := <-done:
		return true, err
	case <-time.After(d):
		return false, nil
	}
}

// dial abre uma conexão com o servidor (buffers pequenos) e a fecha no fim do teste.
func (ts *testServer) dial() *net.TCPConn {
	ts.t.Helper()
	c, err := net.DialTimeout("tcp", ts.addr, 5*time.Second)
	if err != nil {
		ts.t.Fatalf("conectando ao servidor: %v", err)
	}
	tc := c.(*net.TCPConn)
	tc.SetReadBuffer(32 << 10)
	tc.SetWriteBuffer(32 << 10)
	ts.t.Cleanup(func() { tc.Close() })
	return tc
}

// roundTrip envia p e espera o eco em até d.
func roundTrip(c net.Conn, p []byte, d time.Duration) error {
	c.SetDeadline(time.Now().Add(d))
	defer c.SetDeadline(time.Time{})
	if _, err := c.Write(encode(p)); err != nil {
		return fmt.Errorf("enviando %s: %w", describe(p), err)
	}
	got, err := decode(c)
	if err != nil {
		return fmt.Errorf("esperando o eco de %s: %w", describe(p), err)
	}
	if !bytes.Equal(got, p) {
		return fmt.Errorf("eco diferente do enviado: %s", mismatch(p, got))
	}
	return nil
}

// closedByPeer espera até d pelo fechamento da conexão pelo servidor,
// descartando o que chegar. Devolve o erro final da leitura e quando ele veio.
func closedByPeer(c net.Conn, d time.Duration) (after time.Duration, err error) {
	start := time.Now()
	c.SetReadDeadline(start.Add(d))
	buf := make([]byte, 32<<10)
	for {
		if _, err = c.Read(buf); err != nil {
			return time.Since(start), err
		}
	}
}

func isTimeout(err error) bool {
	var ne net.Error
	return errors.As(err, &ne) && ne.Timeout()
}

// chopProxy repassa bytes entre o cliente e o servidor em pedaços pequenos e
// com pausas — como uma rede real com MTU, perdas e retransmissões — para que
// o servidor veja cabeçalhos e corpos chegando aos pedaços.
type chopProxy struct {
	ln   net.Listener
	wg   sync.WaitGroup
	mu   sync.Mutex
	open []net.Conn
}

func startChopProxy(t *testing.T, target string, seed uint64) *chopProxy {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	p := &chopProxy{ln: ln}
	p.wg.Add(1)
	go func() {
		defer p.wg.Done()
		for i := uint64(0); ; i++ {
			down, err := ln.Accept()
			if err != nil {
				return
			}
			up, err := net.Dial("tcp", target)
			if err != nil {
				down.Close()
				continue
			}
			p.mu.Lock()
			p.open = append(p.open, down, up)
			p.mu.Unlock()
			p.wg.Add(2)
			go p.pump(up, down, seed+2*i)
			go p.pump(down, up, seed+2*i+1)
		}
	}()
	t.Cleanup(p.close)
	return p
}

var chunkSizes = []int{1, 1, 2, 3, 5, 7, 64, 1000, 4093}

func (p *chopProxy) pump(dst, src net.Conn, seed uint64) {
	defer p.wg.Done()
	r := rand.New(rand.NewPCG(seed, 22))
	buf := make([]byte, 64<<10)
	for {
		n, err := src.Read(buf)
		for b := buf[:n]; len(b) > 0; {
			k := min(chunkSizes[r.IntN(len(chunkSizes))], len(b))
			if _, werr := dst.Write(b[:k]); werr != nil {
				return
			}
			b = b[k:]
			if r.IntN(40) == 0 {
				time.Sleep(time.Millisecond)
			}
		}
		if err != nil {
			if tc, ok := dst.(*net.TCPConn); ok {
				tc.CloseWrite()
			}
			return
		}
	}
}

func (p *chopProxy) addr() string { return p.ln.Addr().String() }

func (p *chopProxy) close() {
	p.ln.Close()
	p.mu.Lock()
	for _, c := range p.open {
		c.Close()
	}
	p.mu.Unlock()
	p.wg.Wait()
}

var goroutineHeader = regexp.MustCompile(`^goroutine (\d+) `)

// goroutinesWith devolve id -> pilha das goroutines com frames de pkg.
func goroutinesWith(pkg string) map[string]string {
	buf := make([]byte, 8<<20)
	n := runtime.Stack(buf, true)
	out := map[string]string{}
	for _, g := range strings.Split(string(buf[:n]), "\n\n") {
		m := goroutineHeader.FindStringSubmatch(g)
		if m != nil && strings.Contains(g, pkg) {
			out[m[1]] = g
		}
	}
	return out
}

// leakCheck tira uma foto das goroutines de pkg agora; a função devolvida
// falha o teste se, depois de um período de tolerância, existirem goroutines
// novas de pkg (criadas durante o teste e nunca encerradas).
func leakCheck(t *testing.T, pkg string, grace time.Duration) func(what string) {
	t.Helper()
	before := goroutinesWith(pkg)
	return func(what string) {
		t.Helper()
		deadline := time.Now().Add(grace)
		for {
			var leaked []string
			for id, stack := range goroutinesWith(pkg) {
				if _, old := before[id]; !old {
					leaked = append(leaked, stack)
				}
			}
			if len(leaked) == 0 {
				return
			}
			if time.Now().After(deadline) {
				shown := leaked[:min(len(leaked), 3)]
				t.Errorf("%s: %d goroutine(s) de %s continuam vivas (vazamento). Algumas:\n%s",
					what, len(leaked), strings.TrimSuffix(pkg, "."), strings.Join(shown, "\n\n"))
				return
			}
			time.Sleep(20 * time.Millisecond)
		}
	}
}
