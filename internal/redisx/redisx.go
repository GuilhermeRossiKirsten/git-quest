// Package redisx é um cliente Redis mínimo (protocolo RESP2) escrito só com a
// biblioteca padrão: um pool de conexões TCP e os poucos comandos de que o
// catálogo e os testes precisam.
//
// Faz parte do avaliador — não altere. Comportamento importante:
//
//   - Cada chamada respeita o ctx recebido: o prazo (deadline) do ctx vira o
//     prazo de leitura/escrita da conexão e o cancelamento interrompe a
//     chamada. Sem prazo no ctx, uma chamada espera o Redis PARA SEMPRE.
//   - Um erro de rede ou de prazo descarta a conexão (nunca volta ao pool com
//     uma resposta pendente); um erro devolvido pelo servidor (tipo Error) não.
package redisx

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"strconv"
	"sync"
	"time"
)

// Error é uma resposta de erro do servidor (ex.: "WRONGTYPE ...").
type Error string

func (e Error) Error() string { return "redis: " + string(e) }

// ErrClosed é devolvido depois de Close.
var ErrClosed = errors.New("redisx: cliente fechado")

const maxIdle = 32

// Client é seguro para uso concorrente.
type Client struct {
	addr string
	db   int

	mu     sync.Mutex
	idle   []*conn
	closed bool
}

type conn struct {
	nc net.Conn
	br *bufio.Reader
	bw *bufio.Writer
}

// New cria um cliente para addr ("host:porta") usando o banco lógico db
// (SELECT db). Não conecta: a primeira conexão é aberta na primeira chamada.
func New(addr string, db int) *Client { return &Client{addr: addr, db: db} }

// Addr devolve o endereço configurado.
func (c *Client) Addr() string { return c.addr }

// Close fecha as conexões ociosas; chamadas posteriores falham com ErrClosed.
func (c *Client) Close() error {
	c.mu.Lock()
	idle := c.idle
	c.idle, c.closed = nil, true
	c.mu.Unlock()
	for _, cn := range idle {
		_ = cn.nc.Close()
	}
	return nil
}

// Do envia um comando e devolve a resposta: string (simple/bulk string),
// int64, nil (bulk/array nulo), []any (array) ou um erro.
func (c *Client) Do(ctx context.Context, args ...string) (any, error) {
	if len(args) == 0 {
		return nil, errors.New("redisx: comando vazio")
	}
	if err := ctx.Err(); err != nil {
		return nil, fmt.Errorf("redisx %s: %w", args[0], err)
	}
	cn, err := c.get(ctx)
	if err != nil {
		return nil, c.wrap(ctx, args[0], err)
	}

	deadline, _ := ctx.Deadline() // zero = sem prazo
	_ = cn.nc.SetDeadline(deadline)
	stop := context.AfterFunc(ctx, func() { _ = cn.nc.SetDeadline(time.Unix(1, 0)) })
	reply, err := cn.roundTrip(args)
	interrupted := !stop()

	var srvErr Error
	switch {
	case err != nil && !errors.As(err, &srvErr):
		_ = cn.nc.Close()
		return nil, c.wrap(ctx, args[0], err)
	case interrupted:
		// O ctx terminou durante a chamada: o prazo da conexão foi alterado.
		_ = cn.nc.Close()
	default:
		c.put(cn)
	}
	return reply, err
}

func (c *Client) wrap(ctx context.Context, cmd string, err error) error {
	if ctxErr := ctx.Err(); ctxErr != nil {
		return fmt.Errorf("redisx %s: %w (%v)", cmd, ctxErr, err)
	}
	return fmt.Errorf("redisx %s: %w", cmd, err)
}

func (c *Client) get(ctx context.Context) (*conn, error) {
	c.mu.Lock()
	if c.closed {
		c.mu.Unlock()
		return nil, ErrClosed
	}
	if n := len(c.idle); n > 0 {
		cn := c.idle[n-1]
		c.idle = c.idle[:n-1]
		c.mu.Unlock()
		return cn, nil
	}
	c.mu.Unlock()

	var d net.Dialer
	nc, err := d.DialContext(ctx, "tcp", c.addr)
	if err != nil {
		return nil, err
	}
	cn := &conn{nc: nc, br: bufio.NewReader(nc), bw: bufio.NewWriter(nc)}
	if c.db != 0 {
		deadline, _ := ctx.Deadline()
		_ = nc.SetDeadline(deadline)
		if _, err := cn.roundTrip([]string{"SELECT", strconv.Itoa(c.db)}); err != nil {
			_ = nc.Close()
			return nil, err
		}
	}
	return cn, nil
}

func (c *Client) put(cn *conn) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.closed || len(c.idle) >= maxIdle {
		_ = cn.nc.Close()
		return
	}
	c.idle = append(c.idle, cn)
}

func (cn *conn) roundTrip(args []string) (any, error) {
	fmt.Fprintf(cn.bw, "*%d\r\n", len(args))
	for _, a := range args {
		fmt.Fprintf(cn.bw, "$%d\r\n%s\r\n", len(a), a)
	}
	if err := cn.bw.Flush(); err != nil {
		return nil, err
	}
	return readReply(cn.br)
}

func readReply(br *bufio.Reader) (any, error) {
	line, err := br.ReadString('\n')
	if err != nil {
		return nil, err
	}
	if len(line) < 3 || line[len(line)-2] != '\r' {
		return nil, fmt.Errorf("resposta RESP malformada: %q", line)
	}
	kind, body := line[0], line[1:len(line)-2]
	switch kind {
	case '+':
		return body, nil
	case '-':
		return nil, Error(body)
	case ':':
		return strconv.ParseInt(body, 10, 64)
	case '$':
		n, err := strconv.Atoi(body)
		if err != nil {
			return nil, err
		}
		if n < 0 {
			return nil, nil
		}
		buf := make([]byte, n+2)
		if _, err := io.ReadFull(br, buf); err != nil {
			return nil, err
		}
		return string(buf[:n]), nil
	case '*':
		n, err := strconv.Atoi(body)
		if err != nil {
			return nil, err
		}
		if n < 0 {
			return nil, nil
		}
		out := make([]any, n)
		var firstErr error
		for i := range out {
			v, err := readReply(br)
			var srvErr Error
			if err != nil && !errors.As(err, &srvErr) {
				return nil, err
			}
			if err != nil && firstErr == nil {
				firstErr = err
			}
			out[i] = v
		}
		return out, firstErr
	}
	return nil, fmt.Errorf("tipo RESP desconhecido %q", kind)
}

// Get devolve o valor da chave; ok=false se ela não existe.
func (c *Client) Get(ctx context.Context, key string) (val string, ok bool, err error) {
	r, err := c.Do(ctx, "GET", key)
	if err != nil || r == nil {
		return "", false, err
	}
	s, isStr := r.(string)
	if !isStr {
		return "", false, fmt.Errorf("redisx GET: resposta inesperada %T", r)
	}
	return s, true, nil
}

// Set grava a chave. ttl > 0 define a expiração (SET ... PX ms); ttl == 0 grava sem expiração.
func (c *Client) Set(ctx context.Context, key, val string, ttl time.Duration) error {
	args := []string{"SET", key, val}
	if ttl > 0 {
		args = append(args, "PX", strconv.FormatInt(ttl.Milliseconds(), 10))
	}
	_, err := c.Do(ctx, args...)
	return err
}

// SetNX grava a chave só se ela não existir (SET ... NX PX ms); devolve se gravou.
func (c *Client) SetNX(ctx context.Context, key, val string, ttl time.Duration) (bool, error) {
	args := []string{"SET", key, val, "NX"}
	if ttl > 0 {
		args = append(args, "PX", strconv.FormatInt(ttl.Milliseconds(), 10))
	}
	r, err := c.Do(ctx, args...)
	return err == nil && r != nil, err
}

// Del apaga as chaves e devolve quantas existiam.
func (c *Client) Del(ctx context.Context, keys ...string) (int64, error) {
	r, err := c.Do(ctx, append([]string{"DEL"}, keys...)...)
	if err != nil {
		return 0, err
	}
	n, _ := r.(int64)
	return n, nil
}

// PTTL devolve o tempo de vida restante em milissegundos: -1 = sem expiração,
// -2 = a chave não existe.
func (c *Client) PTTL(ctx context.Context, key string) (int64, error) {
	r, err := c.Do(ctx, "PTTL", key)
	if err != nil {
		return 0, err
	}
	n, _ := r.(int64)
	return n, nil
}

// Eval executa um script Lua (EVAL script numkeys keys... args...).
func (c *Client) Eval(ctx context.Context, script string, keys []string, args ...string) (any, error) {
	cmd := append([]string{"EVAL", script, strconv.Itoa(len(keys))}, keys...)
	return c.Do(ctx, append(cmd, args...)...)
}

// Keys lista as chaves que casam com pattern usando SCAN (não bloqueia o servidor como KEYS).
func (c *Client) Keys(ctx context.Context, pattern string) ([]string, error) {
	var out []string
	cursor := "0"
	for {
		r, err := c.Do(ctx, "SCAN", cursor, "MATCH", pattern, "COUNT", "500")
		if err != nil {
			return nil, err
		}
		arr, ok := r.([]any)
		if !ok || len(arr) != 2 {
			return nil, fmt.Errorf("redisx SCAN: resposta inesperada %v", r)
		}
		cursor, _ = arr[0].(string)
		batch, _ := arr[1].([]any)
		for _, k := range batch {
			if s, ok := k.(string); ok {
				out = append(out, s)
			}
		}
		if cursor == "0" {
			return out, nil
		}
	}
}

// FlushDB apaga todas as chaves do banco lógico do cliente.
func (c *Client) FlushDB(ctx context.Context) error {
	_, err := c.Do(ctx, "FLUSHDB")
	return err
}

// Ping verifica a conexão.
func (c *Client) Ping(ctx context.Context) error {
	_, err := c.Do(ctx, "PING")
	return err
}
