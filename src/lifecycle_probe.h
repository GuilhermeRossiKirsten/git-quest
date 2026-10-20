// lifecycle_probe.h — sonda de cópias/movimentos usada pelos testes. NÃO edite.
//
// Message guarda um membro LifecycleProbe<Message>. Como é um membro comum,
// ele é copiado/movido junto com a Message pelas funções membro especiais
// geradas pelo compilador. Se você escrever alguma delas à mão, copie/mova
// também o probe_ (ex.: probe_(other.probe_) ou probe_(std::move(other.probe_))).
#ifndef GITQUEST_LIFECYCLE_PROBE_H
#define GITQUEST_LIFECYCLE_PROBE_H

template <typename Tag>
class LifecycleProbe {
public:
    LifecycleProbe() {}
    LifecycleProbe(const LifecycleProbe&) { ++copies_; }
    LifecycleProbe(LifecycleProbe&&) noexcept { ++moves_; }
    LifecycleProbe& operator=(const LifecycleProbe&) {
        ++copies_;
        return *this;
    }
    LifecycleProbe& operator=(LifecycleProbe&&) noexcept {
        ++moves_;
        return *this;
    }

    static long copies() { return copies_; }
    static long moves() { return moves_; }
    static void reset() { copies_ = moves_ = 0; }

private:
    static long copies_;
    static long moves_;
};

template <typename Tag>
long LifecycleProbe<Tag>::copies_ = 0;
template <typename Tag>
long LifecycleProbe<Tag>::moves_ = 0;

#endif
