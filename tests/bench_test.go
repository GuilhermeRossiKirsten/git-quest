package pool_test

import (
	"context"
	"crypto/sha256"
	"fmt"
	"testing"

	"gitquest/pool/src/pool"
)

// BenchmarkRun é material de estudo, não critério de conclusão:
//
//	go test -run '^$' -bench . -benchmem ./tests/
//
// Compare workers=1 com workers=GOMAXPROCS e com valores bem maiores: a
// partir de certo ponto, mais goroutines só acrescentam troca de contexto.
func BenchmarkRun(b *testing.B) {
	inputs := make([][]byte, 256)
	for i := range inputs {
		inputs[i] = make([]byte, 4096)
		inputs[i][0] = byte(i)
	}
	hash := func(_ context.Context, p []byte) ([32]byte, error) {
		return sha256.Sum256(p), nil
	}
	for _, w := range []int{1, 2, 4, 16, 256} {
		b.Run(fmt.Sprintf("workers=%d", w), func(b *testing.B) {
			for b.Loop() {
				if _, err := pool.Run(context.Background(), w, inputs, hash); err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}
