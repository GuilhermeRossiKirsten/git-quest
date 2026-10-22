package tests

// Testes do servidor de eco sobre TCP de verdade (loopback do container).
// Os clientes usam o codificador/decodificador de referência (encode/decode).

import (
	"bytes"
	"errors"
	"math/rand/v2"
	"net"
	"sync"
	"testing"
	"time"

	"desafio22/src/echo"
)

func TestServer_EcoBasico(t *testing.T) {
	ts := startServer(t, &echo.Server{})
	c := ts.dial()
	for i, n := range []int{0, 1, 10, 100, 1000, 4} {
		if err := roundTrip(c, payload(i, n), 5*time.Second); err != nil {
			t.Fatalf("mensagem %d: %v", i, err)
		}
	}
}

// pipeline envia todos os frames de uma vez (sem esperar respostas) e, em
// paralelo, confere que os ecos chegam íntegros e na mesma ordem.
func pipeline(t *testing.T, conn net.Conn, msgs [][]byte, limit time.Duration) {
	t.Helper()
	conn.SetDeadline(time.Now().Add(limit))
	writeErr := make(chan error, 1)
	go func() {
		for _, m := range msgs {
			if _, err := conn.Write(encode(m)); err != nil {
				writeErr <- err
				return
			}
		}
		writeErr <- nil
	}()
	defer func() {
		conn.Close() // destrava o escritor se a leitura parou antes
		<-writeErr
	}()
	for i, want := range msgs {
		got, err := decode(conn)
		if err != nil {
			t.Fatalf("eco %d de %d (%s): %v", i, len(msgs), describe(want), err)
		}
		if !bytes.Equal(got, want) {
			t.Fatalf("eco %d de %d: %s — respostas fora de ordem ou frames misturados",
				i, len(msgs), mismatch(want, got))
		}
	}
}

func TestServer_FragmentacaoTCP(t *testing.T) {
	ts := startServer(t, &echo.Server{IdleTimeout: 20 * time.Second})
	proxy := startChopProxy(t, ts.addr, 2222)
	conn, err := net.DialTimeout("tcp", proxy.addr(), 5*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()

	r := rand.New(rand.NewPCG(22, 1))
	var msgs [][]byte
	big := map[int]int{20: 65536, 45: 70000, 90: 300000, 130: maxFrame}
	for i := range 160 {
		n := r.IntN(600)
		if b, ok := big[i]; ok {
			n = b
		}
		msgs = append(msgs, payload(i, n))
	}
	pipeline(t, conn, msgs, 90*time.Second)
}

func TestServer_PipelineEmOrdem(t *testing.T) {
	ts := startServer(t, &echo.Server{IdleTimeout: 20 * time.Second})
	conn := ts.dial()
	r := rand.New(rand.NewPCG(22, 2))
	msgs := make([][]byte, 3000)
	for i := range msgs {
		msgs[i] = payload(i, 8+r.IntN(3000))
	}
	pipeline(t, conn, msgs, 60*time.Second)
}

func TestServer_IdleTimeout(t *testing.T) {
	const idle = 400 * time.Millisecond
	ts := startServer(t, &echo.Server{IdleTimeout: idle, WriteTimeout: 2 * time.Second})

	t.Run("conexão muda é derrubada", func(t *testing.T) {
		t.Parallel()
		c := ts.dial()
		after, err := closedByPeer(c, 4*time.Second)
		if isTimeout(err) {
			t.Fatalf("cliente conectado sem enviar nada continuou conectado por 4s (IdleTimeout = %v)", idle)
		}
		if after < idle*3/4 {
			t.Fatalf("conexão derrubada cedo demais: %v (IdleTimeout = %v)", after.Round(time.Millisecond), idle)
		}
	})

	t.Run("slowloris (1 byte a cada 100ms, frame nunca completa) é derrubado", func(t *testing.T) {
		t.Parallel()
		c := ts.dial()
		data := encode(payload(1, 40)) // 44 bytes → 4,4 s para completar
		done := make(chan struct{})
		go func() {
			defer close(done)
			for _, b := range data {
				if _, err := c.Write([]byte{b}); err != nil {
					return
				}
				time.Sleep(100 * time.Millisecond)
			}
		}()
		after, err := closedByPeer(c, 4*time.Second)
		c.Close()
		<-done
		if isTimeout(err) {
			t.Fatalf("cliente que pinga 1 byte a cada 100ms sem nunca completar um frame continuou conectado por 4s (IdleTimeout = %v): "+
				"o prazo precisa valer para o frame inteiro, não para cada Read", idle)
		}
		if after > 3*time.Second {
			t.Fatalf("slowloris derrubado só depois de %v (IdleTimeout = %v)", after.Round(time.Millisecond), idle)
		}
	})

	t.Run("cliente lento, mas ativo, continua conectado", func(t *testing.T) {
		t.Parallel()
		c := ts.dial()
		start := time.Now()
		for i := range 15 { // um frame a cada 100ms durante ~1,5 s (> IdleTimeout)
			if err := roundTrip(c, payload(i, 32), 2*time.Second); err != nil {
				t.Fatalf("depois de %v de conversa ativa (frame %d): %v — o servidor derrubou um cliente que nunca ficou ocioso por %v",
					time.Since(start).Round(time.Millisecond), i, err, idle)
			}
			time.Sleep(100 * time.Millisecond)
		}
	})
}

// flood envia frames de 256 bytes sem ler nada, até um Write ficar bloqueado
// por 1 s, a conexão cair ou limit bytes serem aceitos. Devolve quanto enviou.
// (Payload zerado: se o servidor perder o alinhamento dos frames, os
// "cabeçalhos" que ele lê do meio do fluxo anunciam no máximo 16 MiB.)
func flood(c net.Conn, limit int) (sent int, err error) {
	f := encode(make([]byte, 256))
	for sent < limit {
		c.SetWriteDeadline(time.Now().Add(time.Second))
		if _, err = c.Write(f); err != nil {
			break
		}
		sent += len(f)
	}
	c.SetWriteDeadline(time.Time{})
	return sent, err
}

func TestServer_Backpressure(t *testing.T) {
	const limit = 4 << 20
	ts := startServer(t, &echo.Server{IdleTimeout: 20 * time.Second, WriteTimeout: 20 * time.Second})
	greedy := ts.dial()

	sent, err := flood(greedy, limit)
	if sent >= limit {
		t.Errorf("o servidor aceitou %s de um cliente que não lê nenhuma resposta, sem frear nem desconectar: "+
			"as respostas estão se acumulando na memória do servidor (os buffers de socket aqui são de ~64 KiB)", human(uint64(sent)))
	} else {
		t.Logf("cliente que não lê foi contido depois de %s (%v)", human(uint64(sent)), err)
	}

	other := ts.dial()
	for i := range 5 {
		if err := roundTrip(other, payload(i, 100), 3*time.Second); err != nil {
			t.Fatalf("outro cliente não foi atendido enquanto um consumidor lento está conectado: %v", err)
		}
	}
}

func TestServer_ConsumidorLento(t *testing.T) {
	const wt = 300 * time.Millisecond
	ts := startServer(t, &echo.Server{IdleTimeout: 20 * time.Second, WriteTimeout: wt})
	c := ts.dial()
	flood(c, 4<<20)
	_, err := closedByPeer(c, 5*time.Second)
	if isTimeout(err) {
		t.Fatalf("o cliente parou de ler há segundos e a conexão continua aberta: com WriteTimeout = %v, "+
			"o servidor deveria desistir da escrita e derrubar o consumidor lento", wt)
	}
}

func TestServer_CloseEncerraTudo(t *testing.T) {
	check := leakCheck(t, echoPkg, 2*time.Second)
	srv := &echo.Server{IdleTimeout: 30 * time.Second, WriteTimeout: 30 * time.Second}
	ts := startServer(t, srv)

	idle := ts.dial()
	if err := roundTrip(idle, payload(1, 10), 5*time.Second); err != nil {
		t.Fatal(err)
	}
	half := ts.dial()
	if err := roundTrip(half, payload(2, 10), 5*time.Second); err != nil {
		t.Fatal(err)
	}
	half.Write(encode(payload(3, 10))[:6]) // cabeçalho + 2 bytes de 10

	greedy := ts.dial()
	if err := roundTrip(greedy, payload(4, 10), 5*time.Second); err != nil {
		t.Fatal(err)
	}
	var wg sync.WaitGroup
	wg.Add(1)
	go func() { defer wg.Done(); flood(greedy, 2<<20) }()
	time.Sleep(500 * time.Millisecond) // deixa as escritas do servidor travarem

	start := time.Now()
	returned, _ := closeWithin(srv, 3*time.Second)
	closeTook := time.Since(start)
	if !returned {
		t.Fatalf("Close não retornou em 3s")
	}
	select {
	case err := <-ts.serveErr:
		if !errors.Is(err, echo.ErrServerClosed) {
			t.Errorf("Serve devolveu %v; esperado echo.ErrServerClosed", err)
		}
	case <-time.After(2 * time.Second):
		t.Errorf("Serve não retornou 2s depois de Close")
	}
	for _, cc := range []struct {
		name string
		conn net.Conn
	}{{"ociosa", idle}, {"no meio de um frame", half}, {"que não lê as respostas", greedy}} {
		if _, err := closedByPeer(cc.conn, time.Second); isTimeout(err) {
			t.Errorf("conexão %s continuou aberta depois de Close (que retornou em %v)",
				cc.name, closeTook.Round(time.Millisecond))
		}
	}
	if c, err := net.DialTimeout("tcp", ts.addr, time.Second); err == nil {
		c.Close()
		t.Errorf("o servidor ainda aceita conexões depois de Close")
	}
	greedy.Close()
	wg.Wait()
	check("depois de Close")
}
