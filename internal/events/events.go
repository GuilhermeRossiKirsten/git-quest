// Package events define os contratos de mensagens trocadas pelo serviço de
// pagamentos. Ele é compartilhado com outros times e NÃO faz parte do desafio:
// não altere estes tipos.
package events

import "context"

// PaymentRequested chega pela fila "payments.requested". A fila entrega cada
// mensagem PELO MENOS uma vez (at-least-once): a mesma mensagem, com o mesmo
// MessageID, pode chegar de novo — inclusive em paralelo, a outro consumidor.
type PaymentRequested struct {
	MessageID   string `json:"message_id"`
	AccountID   string `json:"account_id"`
	AmountCents int64  `json:"amount_cents"`
}

// Tipos de evento publicados no tópico "payments.events".
const (
	TypePaymentCompleted = "PaymentCompleted" // cliente cobrado
	TypePaymentDeclined  = "PaymentDeclined"  // saldo insuficiente, nada cobrado
)

// Event é o que os consumidores downstream (pedidos, notificações) recebem.
// Eles descartam eventos repetidos pelo ID: publicar o MESMO evento (mesmo ID)
// mais de uma vez é inofensivo; dois eventos com IDs diferentes para o mesmo
// pagamento são tratados como dois pagamentos.
type Event struct {
	ID          string `json:"id"`
	Type        string `json:"type"`
	PaymentID   string `json:"payment_id"` // = PaymentRequested.MessageID
	AccountID   string `json:"account_id"`
	AmountCents int64  `json:"amount_cents"`
}

// Publisher envia eventos ao broker. Publish pode falhar (broker indisponível);
// quando retorna nil, o evento foi aceito pelo broker.
type Publisher interface {
	Publish(ctx context.Context, e Event) error
}
