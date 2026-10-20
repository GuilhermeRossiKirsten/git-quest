// instance_counter.h — contador de instâncias vivas usado pelos testes.
//
// NÃO faz parte do que você precisa corrigir: basta manter um membro
// InstanceCounter<Session> em Session. Ele conta corretamente construções por
// cópia e por movimento, então funciona com qualquer conjunto de funções
// membro especiais (inclusive as geradas pelo compilador).
#ifndef GITQUEST_INSTANCE_COUNTER_H
#define GITQUEST_INSTANCE_COUNTER_H

template <typename Tag>
class InstanceCounter {
public:
    InstanceCounter() { ++live_; }
    InstanceCounter(const InstanceCounter&) { ++live_; }
    InstanceCounter(InstanceCounter&&) noexcept { ++live_; }
    InstanceCounter& operator=(const InstanceCounter&) { return *this; }
    InstanceCounter& operator=(InstanceCounter&&) noexcept { return *this; }
    ~InstanceCounter() { --live_; }

    static long live() { return live_; }

private:
    static long live_;
};

template <typename Tag>
long InstanceCounter<Tag>::live_ = 0;

#endif
