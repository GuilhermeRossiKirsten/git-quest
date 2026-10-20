package tests

import (
	"testing"
	"time"

	"gitquest/payments/internal/events"
	"gitquest/payments/internal/sim"
)

// Sem falhas: cobra quem tem saldo, recusa quem não tem, um evento por pagamento.
func TestBasico_CobraERecusa(t *testing.T) {
	accs := accounts(2, 1000)
	msgs := []events.PaymentRequested{
		{MessageID: "pay-a1", AccountID: "acc-01", AmountCents: 300},
		{MessageID: "pay-a2", AccountID: "acc-01", AmountCents: 500},
		{MessageID: "pay-a3", AccountID: "acc-01", AmountCents: 400},
		{MessageID: "pay-b1", AccountID: "acc-02", AmountCents: 1000},
		{MessageID: "pay-b2", AccountID: "acc-02", AmountCents: 1},
	}
	s := newScenario(t, 0, accs, msgs, 0)
	ctx := testCtx(t, 30*time.Second)
	s.consume(t, ctx, 1, sim.ConsumerConfig{Workers: 1})
	s.drainRelay(t, ctx, nil)
	rep := s.verify(t)
	if rep.OK() && (rep.Completed != 3 || rep.Declined != 2) {
		t.Errorf("esperava 3 pagamentos confirmados e 2 recusados, veio %d e %d", rep.Completed, rep.Declined)
	}
}

// Cada mensagem chega de 1 a 3 vezes e o broker recusa ~1/4 das primeiras
// publicações (falha transitória).
func TestReentrega_SemCobrancaDupla(t *testing.T) {
	accs := accounts(3, 20000)
	msgs := sim.Messages(11, 120, accs, 100, 900)
	s := newScenario(t, 0, accs, msgs, 4)
	ctx := testCtx(t, 60*time.Second)
	s.consume(t, ctx, 3, sim.ConsumerConfig{Workers: 1})
	s.drainRelay(t, ctx, nil)
	s.verify(t)
}

// O processo consumidor morre em pontos diferentes do processamento (antes do
// 1º, 2º, ..., 10º passo — comando SQL ou publicação — ou depois de terminar,
// antes do ack) e é reiniciado; a mensagem volta para a fila. O relay também
// morre nas primeiras execuções.
func TestCrash_ConsumidorEntreEtapas(t *testing.T) {
	accs := accounts(3, 15000)
	msgs := sim.Messages(23, 100, accs, 100, 900)
	index := map[string]int{}
	for i, m := range msgs {
		index[m.MessageID] = i
	}
	s := newScenario(t, 0, accs, msgs, 0)
	ctx := testCtx(t, 60*time.Second)
	st := s.consume(t, ctx, 2, sim.ConsumerConfig{
		Workers: 1,
		Fault: func(d sim.Delivery) *sim.Fault {
			if d.Copy != 0 || d.Attempt != 1 {
				return nil
			}
			return &sim.Fault{CrashAt: index[d.Msg.MessageID]%11 + 1}
		},
	})
	if st.Crashes == 0 {
		t.Error("nenhuma queda foi simulada — o cenário não exercitou nada")
	}
	s.drainRelay(t, ctx, func(run int) *sim.Fault {
		if run <= 4 {
			return &sim.Fault{CrashAt: run + 1}
		}
		return nil
	})
	s.verify(t)
}

// Oito consumidores na mesma fila, cópias da mesma mensagem chegando ao mesmo
// tempo a consumidores diferentes, poucas contas (muita disputa) e saldo que
// não cobre todos os pedidos. 1 ms de latência por comando, como numa rede real.
func TestConcorrencia_OitoConsumidores(t *testing.T) {
	accs := accounts(4, 10000)
	msgs := sim.Messages(37, 240, accs, 100, 900)
	s := newScenario(t, time.Millisecond, accs, msgs, 7)
	ctx := testCtx(t, 90*time.Second)
	s.consume(t, ctx, 3, sim.ConsumerConfig{Workers: 8})
	s.drainRelay(t, ctx, nil)
	rep := s.verify(t)
	if rep.OK() && rep.Declined == 0 {
		t.Error("nenhum pagamento recusado — o cenário deveria esgotar os saldos")
	}
}
