// Command demo sobe o concentrador (src/echo) em memória, na interface de
// loopback, e reproduz os incidentes relatados pela operação — um por um,
// comparando o que acontece com o que deveria acontecer.
//
//	make demo        (dentro do container — ./start.sh já executa uma vez)
package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"net"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"time"

	"desafio22/src/echo"
	"desafio22/src/frame"
)

func main() {
	if len(os.Args) == 2 && os.Args[1] == "huge-header" {
		hugeHeader()
		return
	}
	scenario("1. Leitura pequena (como no notebook do dev)",
		"eco idêntico", func() (bool, string) {
			srv, addr := start(0, 0)
			defer srv.Close()
			return echoOnce(addr, bytes.Repeat([]byte("p"), 12), nil)
		})

	scenario("2. Leitura de 70 000 bytes (pesagem com foto da placa)",
		"eco idêntico", func() (bool, string) {
			srv, addr := start(0, 0)
			defer srv.Close()
			return echoOnce(addr, bytes.Repeat([]byte("f"), 70000), nil)
		})

	scenario("3. Rede da balança no interior: o frame chega aos pedaços (1 byte a cada 5ms)",
		"eco idêntico", func() (bool, string) {
			srv, addr := start(0, 0)
			defer srv.Close()
			return echoOnce(addr, []byte("balanca-07;31540kg"), func(c net.Conn, b []byte) error {
				for i := range b {
					if _, err := c.Write(b[i : i+1]); err != nil {
						return err
					}
					time.Sleep(5 * time.Millisecond)
				}
				return nil
			})
		})

	scenario("4. Pacote de 4 bytes ff ff ff ff (scanner de portas da rede)",
		"erro ErrTooLarge sem alocar quase nada", func() (bool, string) {
			// Num processo filho: se ReadFrame alocar 4 GiB, quem morre é ele.
			out, err := exec.Command(os.Args[0], "huge-header").CombinedOutput()
			if err != nil {
				return false, fmt.Sprintf("o processo que leu o pacote morreu (%v) — memória esgotada?", err)
			}
			return bytes.HasPrefix(out, []byte("ok")), strings.TrimSpace(string(out))
		})

	scenario("5. Firmware travado: envia leituras sem parar e nunca lê as confirmações",
		"o servidor freia (TCP cheio) ou derruba o cliente; memória estável", func() (bool, string) {
			srv, addr := start(0, 0)
			defer srv.Close()
			c, err := dial(addr)
			if err != nil {
				return false, err.Error()
			}
			defer c.Close()
			g0 := runtime.NumGoroutine()
			sent := 0
			f := encode(make([]byte, 256))
			for sent < 8<<20 {
				c.SetWriteDeadline(time.Now().Add(time.Second))
				if _, err := c.Write(f); err != nil {
					break
				}
				sent += len(f)
			}
			g1 := runtime.NumGoroutine()
			return sent < 4<<20, fmt.Sprintf("servidor aceitou %s sem resposta lida; goroutines: %d → %d", size(uint64(sent)), g0, g1)
		})

	scenario("6. Balança ativa (um frame a cada 200ms) com IdleTimeout = 500ms",
		"8 de 8 confirmações", func() (bool, string) {
			srv, addr := start(500*time.Millisecond, 0)
			defer srv.Close()
			c, err := dial(addr)
			if err != nil {
				return false, err.Error()
			}
			defer c.Close()
			ok := 0
			for i := range 8 {
				if roundTrip(c, []byte(fmt.Sprintf("leitura-%d", i)), nil) != nil {
					break
				}
				ok++
				time.Sleep(200 * time.Millisecond)
			}
			return ok == 8, fmt.Sprintf("%d de 8 confirmações", ok)
		})

	scenario("7. Deploy: Close com uma balança conectada e ociosa",
		"a conexão é fechada e Close retorna", func() (bool, string) {
			srv, addr := start(0, 0)
			c, err := dial(addr)
			if err != nil {
				return false, err.Error()
			}
			defer c.Close()
			if err := roundTrip(c, []byte("oi"), nil); err != nil {
				return false, err.Error()
			}
			srv.Close()
			c.SetReadDeadline(time.Now().Add(time.Second))
			_, err = c.Read(make([]byte, 1))
			var ne net.Error
			if errors.As(err, &ne) && ne.Timeout() {
				return false, "a conexão continuou aberta 1s depois de Close"
			}
			return true, fmt.Sprintf("conexão encerrada (%v)", err)
		})
}

// hugeHeader lê um cabeçalho que anuncia 4 GiB e relata o que aconteceu.
func hugeHeader() {
	var before, after runtime.MemStats
	runtime.ReadMemStats(&before)
	_, err := frame.ReadFrame(bytes.NewReader([]byte{0xff, 0xff, 0xff, 0xff, 1, 2, 3}))
	runtime.ReadMemStats(&after)
	alloc := after.TotalAlloc - before.TotalAlloc
	status := "falha"
	if errors.Is(err, frame.ErrTooLarge) && alloc < 64<<10 {
		status = "ok"
	}
	fmt.Printf("%s: err=%v, alocou %s\n", status, err, size(alloc))
}

func scenario(title, want string, run func() (bool, string)) {
	fmt.Printf("\n%s\n   esperado: %s\n", title, want)
	type result struct {
		ok  bool
		got string
	}
	done := make(chan result, 1)
	go func() {
		ok, got := run()
		done <- result{ok, got}
	}()
	var r result
	select {
	case r = <-done:
	case <-time.After(20 * time.Second):
		r = result{false, "travou por 20s"}
	}
	ok, got := r.ok, r.got
	mark := "✔"
	if !ok {
		mark = "✘"
	}
	fmt.Printf("   obtido:   %s %s\n", mark, got)
}

// start sobe um echo.Server com os prazos dados (0 = padrão) numa porta livre.
func start(idle, writeTimeout time.Duration) (*echo.Server, string) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		panic(err)
	}
	srv := &echo.Server{IdleTimeout: idle, WriteTimeout: writeTimeout,
		Logger: slog.New(slog.DiscardHandler)}
	go srv.Serve(smallBufListener{ln})
	return srv, ln.Addr().String()
}

// smallBufListener reduz os buffers de socket das conexões aceitas (~64 KiB),
// para que o kernel não esconda o quanto o servidor acumula na memória.
type smallBufListener struct{ net.Listener }

func (l smallBufListener) Accept() (net.Conn, error) {
	c, err := l.Listener.Accept()
	if tc, ok := c.(*net.TCPConn); ok {
		tc.SetReadBuffer(32 << 10)
		tc.SetWriteBuffer(32 << 10)
	}
	return c, err
}

func dial(addr string) (*net.TCPConn, error) {
	c, err := net.DialTimeout("tcp", addr, 2*time.Second)
	if err != nil {
		return nil, err
	}
	tc := c.(*net.TCPConn)
	tc.SetReadBuffer(32 << 10)
	tc.SetWriteBuffer(32 << 10)
	return tc, nil
}

func echoOnce(addr string, p []byte, send func(net.Conn, []byte) error) (bool, string) {
	c, err := dial(addr)
	if err != nil {
		return false, err.Error()
	}
	defer c.Close()
	if err := roundTrip(c, p, send); err != nil {
		return false, err.Error()
	}
	return true, fmt.Sprintf("eco idêntico (%d bytes)", len(p))
}

// roundTrip envia p num frame (com send, se dado) e espera o eco por 2s.
func roundTrip(c net.Conn, p []byte, send func(net.Conn, []byte) error) error {
	c.SetDeadline(time.Now().Add(2 * time.Second))
	defer c.SetDeadline(time.Time{})
	if send == nil {
		send = func(c net.Conn, b []byte) error { _, err := c.Write(b); return err }
	}
	if err := send(c, encode(p)); err != nil {
		return fmt.Errorf("envio falhou: %v", err)
	}
	var hdr [4]byte
	if _, err := io.ReadFull(c, hdr[:]); err != nil {
		return fmt.Errorf("sem eco: %v", err)
	}
	n := binary.BigEndian.Uint32(hdr[:])
	if n != uint32(len(p)) {
		return fmt.Errorf("eco anuncia %d bytes, mas foram enviados %d", n, len(p))
	}
	got := make([]byte, n)
	if _, err := io.ReadFull(c, got); err != nil {
		return fmt.Errorf("eco incompleto: %v", err)
	}
	if !bytes.Equal(got, p) {
		return fmt.Errorf("eco com conteúdo diferente")
	}
	return nil
}

func encode(p []byte) []byte {
	b := make([]byte, 4+len(p))
	binary.BigEndian.PutUint32(b, uint32(len(p)))
	copy(b[4:], p)
	return b
}

func size(b uint64) string {
	switch {
	case b >= 1<<30:
		return fmt.Sprintf("%.1f GiB", float64(b)/(1<<30))
	case b >= 1<<20:
		return fmt.Sprintf("%.1f MiB", float64(b)/(1<<20))
	default:
		return fmt.Sprintf("%.1f KiB", float64(b)/(1<<10))
	}
}
