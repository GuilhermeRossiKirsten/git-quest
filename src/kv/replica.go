// Package kv é o armazenamento chave-valor replicado do "Fórum da Vila": cada
// réplica aceita escritas localmente, responde leituras com o que tem e
// propaga as escritas para as outras de forma assíncrona (replicação
// multi-líder, sem coordenador).
package kv

import (
	"log"
	"sync"

	"desafio35/sim"
	"desafio35/src/clock"
)

// version identifica qual escrita produziu o valor atual de uma chave.
type version struct {
	wall int64 // relógio físico da réplica de origem no momento da escrita
}

type entry struct {
	value string
	ver   version
}

// Replica é uma cópia do KV. Os métodos são seguros para uso concorrente.
type Replica struct {
	mu sync.Mutex
	ep sim.Endpoint
	id int

	lamport clock.Lamport
	vc      clock.Vector

	data     map[string]entry
	counters map[string]int64
}

// NewReplica cria a réplica ep.ID() de um grupo de ep.Nodes() réplicas.
// Quem cria a réplica a conecta à rede (sim.Network.Attach).
func NewReplica(ep sim.Endpoint) *Replica {
	return &Replica{
		ep:       ep,
		id:       ep.ID(),
		vc:       clock.NewVector(ep.Nodes()),
		data:     map[string]entry{},
		counters: map[string]int64{},
	}
}

// Put grava key=value nesta réplica e propaga a escrita para as outras.
func (r *Replica) Put(key, value string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	u := r.stamp(update{Op: opPut, Key: key, Value: value})
	r.data[key] = entry{value: value, ver: version{wall: u.Wall}}
	r.broadcast(u)
}

// Add soma delta ao contador key nesta réplica e propaga a operação.
func (r *Replica) Add(key string, delta int64) {
	r.mu.Lock()
	defer r.mu.Unlock()
	u := r.stamp(update{Op: opAdd, Key: key, Delta: delta})
	r.counters[key] += delta
	r.broadcast(u)
}

// Get devolve o valor de key visível nesta réplica.
func (r *Replica) Get(key string) (string, bool) {
	r.mu.Lock()
	defer r.mu.Unlock()
	e, ok := r.data[key]
	return e.value, ok
}

// Counter devolve o valor do contador key visível nesta réplica.
func (r *Replica) Counter(key string) int64 {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.counters[key]
}

// Receive é chamado pela rede para cada mensagem que chega (sim.Node).
func (r *Replica) Receive(from int, payload []byte) {
	u, err := decode(payload)
	if err != nil {
		log.Printf("kv: réplica %d descartou mensagem inválida de %d: %v", r.id, from, err)
		return
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	r.lamport.Observe(u.Lamport)
	r.vc.Merge(u.VC)
	r.apply(u)
}

// stamp preenche origem e carimbos de tempo de uma escrita local.
func (r *Replica) stamp(u update) update {
	u.Origin = r.id
	u.Wall = r.ep.WallClock() // o servidor roda NTP, então o relógio é confiável
	u.Lamport = r.lamport.Tick()
	r.vc.Increment(r.id)
	u.VC = r.vc.Clone()
	return u
}

// apply aplica uma escrita vinda de outra réplica.
func (r *Replica) apply(u update) {
	switch u.Op {
	case opPut:
		cur, ok := r.data[u.Key]
		// Last-writer-wins: a escrita mais recente vence.
		if !ok || u.Wall >= cur.ver.wall {
			r.data[u.Key] = entry{value: u.Value, ver: version{wall: u.Wall}}
		}
	case opAdd:
		r.counters[u.Key] += u.Delta
	}
}

func (r *Replica) broadcast(u update) {
	b := u.encode()
	for to := 0; to < r.ep.Nodes(); to++ {
		if to != r.id {
			r.ep.Send(to, b)
		}
	}
}
