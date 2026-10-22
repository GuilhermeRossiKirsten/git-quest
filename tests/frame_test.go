package tests

// Testes do pacote frame: leituras parciais, EOF, limite de tamanho e o
// formato no fio. As entradas são montadas com o codificador de referência
// (encode), nunca com o código avaliado.

import (
	"bufio"
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math/rand/v2"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"testing"
	"testing/iotest"
	"time"

	"desafio22/src/frame"
)

var streamSizes = []int{0, 1, 5, 300, 4, 65535, 70000, 0, 2}

func stream(sizes []int) []byte {
	var b []byte
	for i, n := range sizes {
		b = append(b, encode(payload(i, n))...)
	}
	return b
}

// chunkReader devolve no máximo um pedaço aleatório (1..7 bytes, às vezes
// mais) por Read — como um socket TCP de verdade pode fazer.
type chunkReader struct {
	r   io.Reader
	rnd *rand.Rand
}

func (c *chunkReader) Read(p []byte) (int, error) {
	k := chunkSizes[c.rnd.IntN(len(chunkSizes))]
	if len(p) > k {
		p = p[:k]
	}
	return c.r.Read(p)
}

// emptyOK atende Read com p vazio sem consultar o leitor de baixo (alguns
// leitores de iotest entram em laço com p vazio).
type emptyOK struct{ r io.Reader }

func (e emptyOK) Read(p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}
	return e.r.Read(p)
}

// readFrame chama frame.ReadFrame, mas falha o teste se ele não retornar em 10s
// (um laço infinito no código avaliado não pode travar a suíte inteira).
func readFrame(t *testing.T, r io.Reader) ([]byte, error) {
	t.Helper()
	type result struct {
		p   []byte
		err error
	}
	done := make(chan result, 1)
	go func() {
		p, err := frame.ReadFrame(emptyOK{r})
		done <- result{p, err}
	}()
	select {
	case res := <-done:
		return res.p, res.err
	case <-time.After(10 * time.Second):
		t.Fatalf("ReadFrame não retornou em 10s (laço infinito? leitura que nunca termina?)")
		return nil, nil
	}
}

func TestFrame_LeiturasParciais(t *testing.T) {
	readers := []struct {
		name string
		wrap func(io.Reader) io.Reader
	}{
		{"um byte por Read (iotest.OneByteReader)", iotest.OneByteReader},
		{"metade por Read (iotest.HalfReader)", iotest.HalfReader},
		{"dados junto com EOF (iotest.DataErrReader)", iotest.DataErrReader},
		{"pedaços aleatórios de 1 a 4093 bytes", func(r io.Reader) io.Reader {
			return &chunkReader{r: r, rnd: rand.New(rand.NewPCG(7, 22))}
		}},
	}
	for _, rd := range readers {
		t.Run(rd.name, func(t *testing.T) {
			r := rd.wrap(bytes.NewReader(stream(streamSizes)))
			for i, n := range streamSizes {
				got, err := readFrame(t, r)
				if err != nil {
					t.Fatalf("frame %d (%d bytes): ReadFrame devolveu erro %v", i, n, err)
				}
				if want := payload(i, n); !bytes.Equal(got, want) {
					t.Fatalf("frame %d: %s — o frame foi montado com bytes errados", i, mismatch(want, got))
				}
			}
			if _, err := readFrame(t, r); !errors.Is(err, io.EOF) {
				t.Fatalf("depois do último frame: esperado io.EOF, recebido %v", err)
			}
		})
	}
}

func TestFrame_EOF(t *testing.T) {
	full := encode(payload(1, 10))
	cases := []struct {
		name   string
		input  []byte
		frames int   // frames completos antes do erro
		want   error // erro esperado depois deles
	}{
		{"fluxo vazio", nil, 0, io.EOF},
		{"frame vazio (tamanho 0) e fim", encode(nil), 1, io.EOF},
		{"dois frames e fim", append(encode(payload(1, 3)), encode(payload(2, 9))...), 2, io.EOF},
		{"cabeçalho cortado (2 de 4 bytes)", full[:2], 0, io.ErrUnexpectedEOF},
		{"cabeçalho completo, corpo ausente", full[:4], 0, io.ErrUnexpectedEOF},
		{"corpo cortado (4 de 10 bytes)", full[:8], 0, io.ErrUnexpectedEOF},
		{"frame completo e depois cabeçalho cortado", append(encode(payload(3, 5)), 0, 0, 0), 1, io.ErrUnexpectedEOF},
	}
	for _, c := range cases {
		for _, oneByte := range []bool{false, true} {
			name := c.name
			if oneByte {
				name += " (1 byte por Read)"
			}
			t.Run(name, func(t *testing.T) {
				var r io.Reader = bytes.NewReader(c.input)
				if oneByte {
					r = iotest.OneByteReader(r)
				}
				for i := range c.frames {
					if _, err := readFrame(t, r); err != nil {
						t.Fatalf("frame completo %d: erro inesperado %v", i, err)
					}
				}
				p, err := readFrame(t, r)
				switch {
				case err == nil:
					t.Fatalf("esperado %v, mas ReadFrame devolveu um frame de %d bytes sem erro", c.want, len(p))
				case c.want == io.EOF && errors.Is(err, io.ErrUnexpectedEOF):
					t.Fatalf("fim limpo do fluxo (entre frames) deve ser io.EOF, recebido %v", err)
				case !errors.Is(err, c.want):
					t.Fatalf("esperado %v, recebido %v", c.want, err)
				}
			})
		}
	}
}

// TestHelper_CabecalhoGigante roda só como processo filho de
// TestFrame_TamanhoMaximo: se ReadFrame alocar o tamanho anunciado (4 GiB),
// quem morre é o filho, não a suíte.
func TestHelper_CabecalhoGigante(t *testing.T) {
	if os.Getenv("GQ22_CHILD") != "huge" {
		t.Skip("processo auxiliar")
	}
	for _, n := range []uint32{0xFFFFFFFF, frame.MaxSize + 1, 0x7FFFFFFF} {
		in := make([]byte, 4, 20)
		binary.BigEndian.PutUint32(in, n)
		in = append(in, bytes.Repeat([]byte{0xAB}, 16)...)
		r := bytes.NewReader(in)
		runtime.GC()
		var before, after runtime.MemStats
		runtime.ReadMemStats(&before)
		_, err := readFrame(t, r)
		runtime.ReadMemStats(&after)
		fmt.Printf("RESULT hdr=%08x alloc=%d toolarge=%v err=%q\n",
			n, after.TotalAlloc-before.TotalAlloc, errors.Is(err, frame.ErrTooLarge), fmt.Sprint(err))
	}
}

func TestFrame_TamanhoMaximo(t *testing.T) {
	t.Run("cabeçalho malicioso não aloca o tamanho anunciado", func(t *testing.T) {
		ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
		defer cancel()
		cmd := exec.CommandContext(ctx, os.Args[0], "-test.run=^TestHelper_CabecalhoGigante$", "-test.v")
		cmd.Env = append(os.Environ(), "GQ22_CHILD=huge")
		out, err := cmd.CombinedOutput()
		var results []string
		sc := bufio.NewScanner(bytes.NewReader(out))
		for sc.Scan() {
			if strings.HasPrefix(sc.Text(), "RESULT ") {
				results = append(results, sc.Text())
			}
		}
		if err != nil || len(results) != 3 {
			tail := string(out)
			if len(tail) > 1500 {
				tail = "…" + tail[len(tail)-1500:]
			}
			t.Fatalf("o processo que leu cabeçalhos de 4 GiB / 2 GiB morreu ou travou (%v) — provavelmente tentou alocar o tamanho anunciado:\n%s", err, tail)
		}
		for _, line := range results {
			var hdr uint32
			var alloc uint64
			var tooLarge bool
			var msg string
			if _, err := fmt.Sscanf(line, "RESULT hdr=%x alloc=%d toolarge=%t err=%q", &hdr, &alloc, &tooLarge, &msg); err != nil {
				t.Fatalf("saída inesperada do processo auxiliar %q: %v", line, err)
			}
			if !tooLarge {
				t.Errorf("cabeçalho 0x%08x (%d bytes): esperado erro que satisfaça errors.Is(err, frame.ErrTooLarge), recebido %s", hdr, hdr, msg)
			}
			if alloc > 64<<10 {
				t.Errorf("cabeçalho 0x%08x: ReadFrame alocou %d bytes antes de recusar o frame (limite: 64 KiB) — um único pacote de 4 bytes custa %s de memória ao servidor",
					hdr, alloc, human(alloc))
			}
		}
	})

	t.Run("frame acima de MaxSize com corpo completo é recusado", func(t *testing.T) {
		in := encode(make([]byte, frame.MaxSize+1))
		p, err := readFrame(t, bytes.NewReader(in))
		if !errors.Is(err, frame.ErrTooLarge) {
			t.Fatalf("esperado frame.ErrTooLarge, recebido err=%v (payload de %d bytes)", err, len(p))
		}
	})

	t.Run("frame de exatamente MaxSize é aceito", func(t *testing.T) {
		want := payload(9, frame.MaxSize)
		got, err := readFrame(t, bytes.NewReader(encode(want)))
		if err != nil || !bytes.Equal(got, want) {
			t.Fatalf("esperado o payload de %d bytes intacto, recebido %s, err=%v", frame.MaxSize, describe(got), err)
		}
	})

	t.Run("WriteFrame recusa payload acima de MaxSize sem escrever nada", func(t *testing.T) {
		var buf bytes.Buffer
		err := frame.WriteFrame(&buf, make([]byte, frame.MaxSize+1))
		if !errors.Is(err, frame.ErrTooLarge) {
			t.Fatalf("esperado frame.ErrTooLarge, recebido %v", err)
		}
		if buf.Len() != 0 {
			t.Fatalf("WriteFrame escreveu %d bytes antes de recusar o payload", buf.Len())
		}
	})
}

func TestFrame_FormatoNoFio(t *testing.T) {
	if frame.HeaderSize != 4 || frame.MaxSize != maxFrame {
		t.Fatalf("constantes do protocolo alteradas: HeaderSize=%d (esperado 4), MaxSize=%d (esperado %d)",
			frame.HeaderSize, frame.MaxSize, maxFrame)
	}
	for _, n := range []int{0, 1, 255, 256, 65535, 65536, 70000, 300000, frame.MaxSize} {
		t.Run(fmt.Sprintf("%d bytes", n), func(t *testing.T) {
			p := payload(n, n)
			var buf bytes.Buffer
			if err := frame.WriteFrame(&buf, p); err != nil {
				t.Fatalf("WriteFrame: %v", err)
			}
			want := encode(p)
			got := buf.Bytes()
			if len(got) < 4 {
				t.Fatalf("WriteFrame escreveu só %d bytes", len(got))
			}
			if !bytes.Equal(got[:4], want[:4]) {
				t.Fatalf("cabeçalho no fio: esperado % x (uint32 big-endian = %d), recebido % x (= %d)",
					want[:4], n, got[:4], binary.BigEndian.Uint32(got[:4]))
			}
			if !bytes.Equal(got, want) {
				t.Fatalf("frame no fio: esperado %d bytes (cabeçalho + payload), recebido %d", len(want), len(got))
			}
			back, err := readFrame(t, bytes.NewReader(got))
			if err != nil || !bytes.Equal(back, p) {
				t.Fatalf("ReadFrame(WriteFrame(p)) != p: %s, err=%v", describe(back), err)
			}
		})
	}
}

func human(b uint64) string {
	switch {
	case b >= 1<<30:
		return fmt.Sprintf("%.1f GiB", float64(b)/(1<<30))
	case b >= 1<<20:
		return fmt.Sprintf("%.1f MiB", float64(b)/(1<<20))
	default:
		return fmt.Sprintf("%d KiB", b>>10)
	}
}
