// Package fetch busca recursos HTTP com novas tentativas (retry) e backoff
// exponencial.
//
// Usado pelo serviço de catálogo para baixar a tabela de preços do fornecedor.
// O contrato completo está no README do desafio.
package fetch

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"time"
)

// DefaultMaxBytes é o limite do corpo quando RetryPolicy.MaxBytes é zero.
const DefaultMaxBytes int64 = 10 << 20 // 10 MiB

// DefaultMaxDelay é o teto do backoff quando RetryPolicy.MaxDelay é zero.
const DefaultMaxDelay = 30 * time.Second

// ErrTooLarge indica que o corpo da resposta excede RetryPolicy.MaxBytes.
var ErrTooLarge = errors.New("fetch: corpo da resposta excede o limite")

// StatusError descreve uma resposta HTTP com status fora da faixa 2xx.
type StatusError struct {
	StatusCode int
}

func (e *StatusError) Error() string {
	return fmt.Sprintf("fetch: status HTTP %d %s", e.StatusCode, http.StatusText(e.StatusCode))
}

// RetryPolicy controla as novas tentativas de Get.
type RetryPolicy struct {
	// MaxAttempts é o número total de tentativas (valores < 1 contam como 1).
	MaxAttempts int
	// BaseDelay é a espera antes da 2ª tentativa; dobra a cada nova falha.
	BaseDelay time.Duration
	// MaxDelay é o teto da espera entre tentativas (0 = DefaultMaxDelay).
	MaxDelay time.Duration
	// MaxBytes limita o tamanho do corpo aceito (0 = DefaultMaxBytes).
	MaxBytes int64
	// Sleep, se não for nil, substitui a espera entre tentativas (útil em
	// testes). Deve retornar ctx.Err() se o contexto terminar antes de d.
	Sleep func(ctx context.Context, d time.Duration) error
}

// Get busca url com c (http.DefaultClient se nil), repetindo a requisição
// quando ela falha.
func Get(ctx context.Context, c *http.Client, url string, p RetryPolicy) ([]byte, error) {
	if c == nil {
		c = http.DefaultClient
	}
	sleep := p.Sleep
	if sleep == nil {
		sleep = func(_ context.Context, d time.Duration) error {
			time.Sleep(d)
			return nil
		}
	}

	var lastErr error
	for attempt := 0; attempt < p.MaxAttempts; attempt++ {
		if attempt > 0 {
			// Backoff exponencial: base, 2·base, 4·base, ...
			delay := p.BaseDelay * time.Duration(1<<(attempt-1))
			_ = sleep(ctx, delay)
		}
		body, err := fetchOnce(c, url)
		if err == nil {
			return body, nil
		}
		lastErr = err
	}
	return nil, fmt.Errorf("fetch %s: falhou após %d tentativas: %v", url, p.MaxAttempts, lastErr)
}

// fetchOnce faz uma única tentativa.
func fetchOnce(c *http.Client, url string) ([]byte, error) {
	resp, err := c.Get(url)
	if err != nil {
		return nil, err
	}
	if resp.StatusCode != http.StatusOK {
		return nil, &StatusError{StatusCode: resp.StatusCode}
	}
	defer resp.Body.Close()
	return io.ReadAll(resp.Body)
}
