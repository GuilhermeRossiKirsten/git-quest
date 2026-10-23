// Package sim é a rede simulada do avaliador: nenhum socket, nenhum relógio de
// parede de verdade. Tudo é determinado pela semente (Config.Seed):
//
//   - cada mensagem leva um atraso sorteado em [MinDelay, MaxDelay] ms, e por
//     isso mensagens enviadas em sequência podem chegar fora de ordem;
//   - com probabilidade DupProb a rede entrega uma cópia extra da mensagem;
//   - nenhuma mensagem se perde (a rede é "pelo menos uma vez");
//   - cada nó tem um relógio físico, WallClock, que é o tempo simulado somado
//     ao desvio (Skew) daquele nó — como um NTP que nunca sincroniza direito.
//
// O tempo só anda quando o avaliador manda (Advance, Step, Drain). Não altere
// este pacote: os testes usam esta versão.
package sim

import (
	"container/heap"
	"fmt"
	"math/rand/v2"
	"sync"
)

// Node é o que uma réplica implementa para receber mensagens da rede.
type Node interface {
	// Receive é chamado pela rede a cada entrega. O payload pertence ao nó.
	Receive(from int, payload []byte)
}

// Endpoint é a visão que um nó tem da rede.
type Endpoint interface {
	ID() int                     // identificador deste nó: 0..Nodes()-1
	Nodes() int                  // quantidade de nós do grupo
	Send(to int, payload []byte) // envio assíncrono; retorna imediatamente
	WallClock() int64            // relógio físico local, em ms (com desvio!)
}

// Config descreve uma rede.
type Config struct {
	Nodes    int
	Seed     uint64
	MinDelay int64   // ms
	MaxDelay int64   // ms
	DupProb  float64 // 0..1: chance de uma cópia extra por mensagem
	Skew     []int64 // desvio do relógio físico de cada nó, em ms (opcional)
}

// Event descreve uma entrega, para quem observa a rede (testes, demo).
type Event struct {
	Time     int64
	From, To int
	Size     int
	Dup      bool
}

func (e Event) String() string {
	d := ""
	if e.Dup {
		d = " (cópia duplicada)"
	}
	return fmt.Sprintf("t=%dms entrega r%d→r%d, %d bytes%s", e.Time, e.From, e.To, e.Size, d)
}

type packet struct {
	at       int64
	seq      uint64
	from, to int
	payload  []byte
	dup      bool
}

type queue []*packet

func (q queue) Len() int { return len(q) }
func (q queue) Less(i, j int) bool {
	if q[i].at != q[j].at {
		return q[i].at < q[j].at
	}
	return q[i].seq < q[j].seq
}
func (q queue) Swap(i, j int) { q[i], q[j] = q[j], q[i] }
func (q *queue) Push(x any)   { *q = append(*q, x.(*packet)) }
func (q *queue) Pop() any {
	old := *q
	p := old[len(old)-1]
	*q = old[:len(old)-1]
	return p
}

// Network é a rede simulada. Os métodos podem ser chamados de várias
// goroutines; Receive dos nós é chamado sem nenhuma trava da rede.
type Network struct {
	mu      sync.Mutex
	cfg     Config
	rng     *rand.Rand
	now     int64
	seq     uint64
	q       queue
	nodes   []Node
	blocked map[[2]int][]*packet
	sent    int

	deliverMu sync.Mutex // serializa as entregas (uma por vez, em ordem)
	onDeliver func(Event)
}

// New cria uma rede com cfg.Nodes nós, ainda sem réplicas conectadas.
func New(cfg Config) *Network {
	if cfg.Nodes <= 0 {
		panic("sim: Nodes precisa ser > 0")
	}
	if cfg.MaxDelay < cfg.MinDelay {
		cfg.MaxDelay = cfg.MinDelay
	}
	skew := make([]int64, cfg.Nodes)
	copy(skew, cfg.Skew)
	cfg.Skew = skew
	return &Network{
		cfg:     cfg,
		rng:     rand.New(rand.NewPCG(cfg.Seed, cfg.Seed^0x9e3779b97f4a7c15)),
		nodes:   make([]Node, cfg.Nodes),
		blocked: map[[2]int][]*packet{},
	}
}

// Endpoint devolve a interface de rede do nó id.
func (n *Network) Endpoint(id int) Endpoint {
	n.check(id)
	return endpoint{n: n, id: id}
}

// Attach conecta o nó que vai receber as mensagens endereçadas a id.
func (n *Network) Attach(id int, node Node) {
	n.check(id)
	n.mu.Lock()
	n.nodes[id] = node
	n.mu.Unlock()
}

// OnDeliver registra uma função chamada depois de cada entrega.
func (n *Network) OnDeliver(f func(Event)) {
	n.mu.Lock()
	n.onDeliver = f
	n.mu.Unlock()
}

// Now devolve o tempo simulado (ms).
func (n *Network) Now() int64 {
	n.mu.Lock()
	defer n.mu.Unlock()
	return n.now
}

// Pending devolve quantas mensagens ainda estão em trânsito (inclui retidas).
func (n *Network) Pending() int {
	n.mu.Lock()
	defer n.mu.Unlock()
	c := len(n.q)
	for _, ps := range n.blocked {
		c += len(ps)
	}
	return c
}

// Sent devolve quantas mensagens os nós enviaram (sem contar duplicatas).
func (n *Network) Sent() int {
	n.mu.Lock()
	defer n.mu.Unlock()
	return n.sent
}

// Block retém as mensagens de from para to até Unblock (enlace lento).
func (n *Network) Block(from, to int) {
	n.check(from)
	n.check(to)
	n.mu.Lock()
	defer n.mu.Unlock()
	k := [2]int{from, to}
	if _, ok := n.blocked[k]; !ok {
		n.blocked[k] = nil
	}
	// Mensagens já em trânsito nesse enlace também ficam retidas.
	kept := n.q[:0]
	for _, p := range n.q {
		if p.from == from && p.to == to {
			n.blocked[k] = append(n.blocked[k], p)
		} else {
			kept = append(kept, p)
		}
	}
	n.q = kept
	heap.Init(&n.q)
}

// Unblock libera o enlace; as mensagens retidas partem agora, com novos atrasos.
func (n *Network) Unblock(from, to int) {
	n.mu.Lock()
	defer n.mu.Unlock()
	k := [2]int{from, to}
	ps := n.blocked[k]
	delete(n.blocked, k)
	for _, p := range ps {
		p.at = n.now + n.delay()
		heap.Push(&n.q, p)
	}
}

// Step entrega a próxima mensagem (avançando o tempo até ela). Devolve false
// se não há nada em trânsito.
func (n *Network) Step() bool {
	n.deliverMu.Lock()
	defer n.deliverMu.Unlock()
	return n.stepLocked(-1)
}

// Advance entrega tudo o que chega até now+dt e então avança o tempo para lá.
func (n *Network) Advance(dt int64) {
	n.deliverMu.Lock()
	defer n.deliverMu.Unlock()
	n.mu.Lock()
	until := n.now + dt
	n.mu.Unlock()
	for n.stepLocked(until) {
	}
	n.mu.Lock()
	if n.now < until {
		n.now = until
	}
	n.mu.Unlock()
}

// Drain entrega mensagens até a rede ficar quieta (mensagens retidas por
// Block não contam). Devolve erro se passar de limit entregas — sinal de que
// os nós ficam trocando mensagens para sempre.
func (n *Network) Drain(limit int) error {
	n.deliverMu.Lock()
	defer n.deliverMu.Unlock()
	for i := 0; ; i++ {
		if i >= limit {
			return fmt.Errorf("sim: a rede não ficou quieta depois de %d entregas", limit)
		}
		if !n.stepLocked(-1) {
			return nil
		}
	}
}

func (n *Network) stepLocked(until int64) bool {
	n.mu.Lock()
	if len(n.q) == 0 || (until >= 0 && n.q[0].at > until) {
		n.mu.Unlock()
		return false
	}
	p := heap.Pop(&n.q).(*packet)
	if p.at > n.now {
		n.now = p.at
	}
	node, hook := n.nodes[p.to], n.onDeliver
	ev := Event{Time: n.now, From: p.from, To: p.to, Size: len(p.payload), Dup: p.dup}
	n.mu.Unlock()
	if node == nil {
		panic(fmt.Sprintf("sim: nenhum nó conectado em %d", p.to))
	}
	node.Receive(p.from, p.payload)
	if hook != nil {
		hook(ev)
	}
	return true
}

func (n *Network) send(from, to int, payload []byte) {
	n.check(to)
	n.mu.Lock()
	defer n.mu.Unlock()
	n.sent++
	copies := 1
	if n.cfg.DupProb > 0 && n.rng.Float64() < n.cfg.DupProb {
		copies = 2
	}
	for c := 0; c < copies; c++ {
		n.seq++
		// Cada entrega recebe sua própria cópia dos bytes.
		p := &packet{at: n.now + n.delay(), seq: n.seq, from: from, to: to,
			payload: append([]byte(nil), payload...), dup: c > 0}
		k := [2]int{from, to}
		if held, ok := n.blocked[k]; ok {
			n.blocked[k] = append(held, p)
			continue
		}
		heap.Push(&n.q, p)
	}
}

func (n *Network) delay() int64 {
	if n.cfg.MaxDelay == n.cfg.MinDelay {
		return n.cfg.MinDelay
	}
	return n.cfg.MinDelay + n.rng.Int64N(n.cfg.MaxDelay-n.cfg.MinDelay+1)
}

func (n *Network) check(id int) {
	if id < 0 || id >= n.cfg.Nodes {
		panic(fmt.Sprintf("sim: nó %d não existe (a rede tem %d)", id, n.cfg.Nodes))
	}
}

type endpoint struct {
	n  *Network
	id int
}

func (e endpoint) ID() int    { return e.id }
func (e endpoint) Nodes() int { return e.n.cfg.Nodes }
func (e endpoint) Send(to int, payload []byte) {
	e.n.send(e.id, to, payload)
}
func (e endpoint) WallClock() int64 {
	e.n.mu.Lock()
	defer e.n.mu.Unlock()
	return e.n.now + e.n.cfg.Skew[e.id]
}
