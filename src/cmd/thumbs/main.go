// Comando thumbs simula o job noturno que gera miniaturas do catálogo da
// Lojinha usando pool.Run. Não há imagens de verdade: cada "redimensionamento"
// dorme alguns milissegundos e devolve o nome da miniatura.
//
// Ele imprime o que a equipe de operações observa em produção: quantas
// conversões rodaram ao mesmo tempo, se cada produto recebeu a própria
// miniatura e quantas goroutines sobraram depois do job.
//
//	go run ./src/cmd/thumbs                 # lote saudável
//	go run ./src/cmd/thumbs -fail 3         # 3 fotos corrompidas no lote
//	go run -race ./src/cmd/thumbs           # com o detector de corridas
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"os"
	"runtime"
	"sync/atomic"
	"time"

	"gitquest/pool/src/pool"
)

// Photo é uma foto de produto aguardando miniatura.
type Photo struct {
	SKU     string
	Bytes   int
	Corrupt bool
}

var errCorrupt = errors.New("arquivo JPEG corrompido")

func main() {
	n := flag.Int("n", 500, "quantidade de fotos no lote")
	workers := flag.Int("workers", 8, "conversões simultâneas permitidas (limite do storage)")
	fail := flag.Int("fail", 0, "quantas fotos do lote estão corrompidas")
	flag.Parse()

	photos := make([]Photo, *n)
	step := max(1, *n/max(1, *fail)) // uma foto corrompida a cada `step`
	for i := range photos {
		photos[i] = Photo{
			SKU:     fmt.Sprintf("SKU-%05d", i),
			Bytes:   200_000 + (i*7919)%800_000,
			Corrupt: *fail > 0 && i%step == step/2,
		}
	}

	var running, peak atomic.Int64
	resize := func(ctx context.Context, p Photo) (string, error) {
		cur := running.Add(1)
		defer running.Add(-1)
		for {
			old := peak.Load()
			if cur <= old || peak.CompareAndSwap(old, cur) {
				break
			}
		}
		if p.Corrupt {
			return "", fmt.Errorf("%s: %w", p.SKU, errCorrupt)
		}
		// Fotos maiores demoram mais para "redimensionar".
		select {
		case <-time.After(time.Duration(p.Bytes/100_000) * time.Millisecond):
		case <-ctx.Done():
			return "", ctx.Err()
		}
		return p.SKU + "_thumb.webp", nil
	}

	before := runtime.NumGoroutine()
	start := time.Now()
	thumbs, err := pool.Run(context.Background(), *workers, photos, resize)
	elapsed := time.Since(start)

	fmt.Printf("lote: %d fotos, workers=%d, tempo=%v\n", *n, *workers, elapsed.Round(time.Millisecond))
	fmt.Printf("pico de conversões simultâneas: %d (limite configurado: %d)\n", peak.Load(), *workers)
	if err != nil {
		fmt.Printf("erro devolvido: %v\n", err)
	} else {
		wrong := 0
		for i, t := range thumbs {
			if i >= len(photos) || t != photos[i].SKU+"_thumb.webp" {
				wrong++
			}
		}
		fmt.Printf("miniaturas devolvidas: %d de %d; associadas ao produto errado: %d\n", len(thumbs), *n, wrong)
	}

	time.Sleep(100 * time.Millisecond) // dá tempo às goroutines que estão terminando
	fmt.Printf("goroutines antes do job: %d, depois: %d\n", before, runtime.NumGoroutine())
	if err != nil {
		os.Exit(1)
	}
}
