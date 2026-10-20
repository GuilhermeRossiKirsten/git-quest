// tracked.h — tipo instrumentado: conta construções, destruições e objetos vivos.
//
// Serve para verificar o tempo de vida dos elementos guardados na fila:
//   live         objetos construídos e ainda não destruídos
//   constructed  construções (padrão, por valor, cópia, movimento)
//   destroyed    destruições
//   misuse       usos de um objeto que não está vivo: destruir duas vezes, copiar de
//                (ou atribuir a) memória onde nenhum Tracked foi construído
#ifndef GQ_TESTS_TRACKED_H
#define GQ_TESTS_TRACKED_H

namespace gq_test {

class Tracked {
public:
    static inline int live = 0;
    static inline int constructed = 0;
    static inline int destroyed = 0;
    static inline int misuse = 0;

    static void reset_counters() { live = constructed = destroyed = misuse = 0; }

    Tracked() noexcept : value_(0) { born(); }
    explicit Tracked(int v) noexcept : value_(v) { born(); }
    Tracked(const Tracked& other) noexcept : value_(other.value_) {
        other.check();
        born();
    }
    Tracked(Tracked&& other) noexcept : value_(other.value_) {
        other.check();
        other.value_ = -1;  // estado "movido"
        born();
    }
    Tracked& operator=(const Tracked& other) noexcept {
        check();
        other.check();
        value_ = other.value_;
        return *this;
    }
    Tracked& operator=(Tracked&& other) noexcept {
        check();
        other.check();
        value_ = other.value_;
        if (this != &other) other.value_ = -1;
        return *this;
    }
    ~Tracked() {
        if (magic_ == kAlive) {
            --live;
        } else {
            ++misuse;
        }
        magic_ = kDead;  // volatile: o compilador não pode descartar esta escrita
        ++destroyed;
    }

    int value() const {
        check();
        return value_;
    }

private:
    static constexpr unsigned kAlive = 0xA11CE5u;
    static constexpr unsigned kDead = 0xDEADu;

    void born() {
        magic_ = kAlive;
        ++live;
        ++constructed;
    }
    void check() const {
        if (magic_ != kAlive) ++misuse;
    }

    int value_;
    volatile unsigned magic_ = 0;
};

}  // namespace gq_test

#endif
