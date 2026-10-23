package kv

import (
	"encoding/json"

	"desafio35/src/clock"
)

// Tipos de atualização replicada.
const (
	opPut = "put" // Put(key, value)
	opAdd = "add" // Add(key, delta)
)

// update é o que uma réplica envia às outras a cada escrita local.
// Formato no fio: JSON (só as réplicas o leem; pode ser alterado à vontade).
type update struct {
	Origin int    `json:"origin"` // réplica que aceitou a escrita do cliente
	Op     string `json:"op"`
	Key    string `json:"key"`
	Value  string `json:"value,omitempty"`
	Delta  int64  `json:"delta,omitempty"`

	// Carimbos de tempo da escrita.
	Wall    int64        `json:"wall"`    // relógio físico da origem (ms)
	Lamport uint64       `json:"lamport"` // relógio de Lamport da origem
	VC      clock.Vector `json:"vc"`      // relógio vetorial da origem
}

func (u update) encode() []byte {
	b, err := json.Marshal(u)
	if err != nil {
		panic(err) // só tipos simples: não acontece
	}
	return b
}

func decode(b []byte) (update, error) {
	var u update
	err := json.Unmarshal(b, &u)
	return u, err
}
