// Package clock reúne os relógios lógicos que o time começou a introduzir no
// KV replicado ("um dia vamos parar de depender do NTP").
package clock

// Lamport é um relógio lógico escalar. O valor zero está pronto para uso.
// Não é seguro para uso concorrente: quem o usa protege com a própria trava.
type Lamport struct {
	t uint64
}

// Now devolve o valor atual, sem gerar evento.
func (c *Lamport) Now() uint64 { return c.t }

// Tick registra um evento local (inclusive um envio) e devolve seu carimbo.
func (c *Lamport) Tick() uint64 {
	c.t++
	return c.t
}

// Observe registra o recebimento de uma mensagem carimbada com remote e
// devolve o carimbo do evento de recebimento.
func (c *Lamport) Observe(remote uint64) uint64 {
	// Receber uma mensagem é um evento local como outro qualquer.
	c.t++
	return c.t
}
