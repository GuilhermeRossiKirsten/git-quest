// Package frame implementa o enquadramento (framing) das mensagens do
// protocolo da Balança: TCP entrega um fluxo de bytes, então cada mensagem
// viaja precedida do seu tamanho.
//
// Formato no fio de um frame:
//
//	+--------------------------------+----------------------+
//	| tamanho: uint32, big-endian    | payload (tamanho     |
//	| (4 bytes, ordem de rede)       | bytes, 0..MaxSize)   |
//	+--------------------------------+----------------------+
package frame

import (
	"encoding/binary"
	"errors"
	"io"
)

// HeaderSize é o tamanho do cabeçalho de cada frame, em bytes.
const HeaderSize = 4

// MaxSize é o maior payload que o protocolo aceita (1 MiB).
const MaxSize = 1 << 20

// ErrTooLarge indica um payload maior que MaxSize.
var ErrTooLarge = errors.New("frame: payload maior que MaxSize")

// WriteFrame escreve p em w como um único frame (cabeçalho + payload).
func WriteFrame(w io.Writer, p []byte) error {
	if len(p) > MaxSize {
		return ErrTooLarge
	}
	var hdr [HeaderSize]byte
	// tamanho em big-endian (ordem de rede)
	hdr[2] = byte(len(p) >> 8)
	hdr[3] = byte(len(p))
	if _, err := w.Write(hdr[:]); err != nil {
		return err
	}
	_, err := w.Write(p)
	return err
}

// ReadFrame lê o próximo frame de r e devolve o payload.
func ReadFrame(r io.Reader) ([]byte, error) {
	var hdr [HeaderSize]byte
	if _, err := r.Read(hdr[:]); err != nil {
		return nil, err
	}
	n := binary.BigEndian.Uint32(hdr[:])

	p := make([]byte, n)
	if _, err := r.Read(p); err != nil {
		return nil, err
	}
	return p, nil
}
