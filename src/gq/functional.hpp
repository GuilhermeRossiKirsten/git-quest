// gq/functional.hpp — utilitários funcionais do pipeline de eventos.
//
// Biblioteca header-only escrita em 2013, "ainda em C++11". Desde então o
// projeto passou a compilar com -std=c++14, mas o código nunca foi revisto.
//
// API pública (os testes do avaliador usam exatamente estes nomes):
//
//   gq::compose(f, g, ...)   composição matemática: compose(f, g)(x) == f(g(x))
//   gq::by(proj)             comparador "menor que" por projeção, para std::sort
//   gq::apply_n(f, n)        callable que aplica f n vezes: apply_n(f, 2)(x) == f(f(x))
//   gq::make_counter(start)  gerador de inteiros: start, start + 1, ...
//   gq::bind_sink(sink)      callable que entrega itens ao sink e é DONO dele
//   gq::at_least(min)        predicado v >= min
//   gq::tagger(tag)          formata mensagens como "[tag] mensagem"
//   gq::is_small_v<T>        true se T é barato de passar por valor
#ifndef GQ_FUNCTIONAL_HPP
#define GQ_FUNCTIONAL_HPP

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace gq {

// ---------------------------------------------------------------------------
// compose
// ---------------------------------------------------------------------------

// Um functor por composição: C++11 não tem lambdas genéricas.
template <class F, class G>
struct Composed {
    F f_;
    G g_;

    Composed(F f, G g) : f_(std::move(f)), g_(std::move(g)) {}

    template <class T>
    auto operator()(T x) const -> decltype(g_(f_(x))) {
        return g_(f_(x));
    }
};

template <class F>
F compose(F f) {
    return f;
}

template <class F, class G>
Composed<F, G> compose(F f, G g) {
    return Composed<F, G>(std::move(f), std::move(g));
}

template <class F, class G, class H, class... Rest>
auto compose(F f, G g, H h, Rest... rest)
    -> decltype(compose(Composed<F, G>(std::move(f), std::move(g)), std::move(h), std::move(rest)...)) {
    return compose(Composed<F, G>(std::move(f), std::move(g)), std::move(h), std::move(rest)...);
}

// ---------------------------------------------------------------------------
// by
// ---------------------------------------------------------------------------

template <class Proj>
struct By {
    Proj proj_;

    template <class T>
    bool operator()(T a, T b) const {
        return proj_(a) < proj_(b);
    }
};

template <class Proj>
By<Proj> by(Proj proj) {
    return By<Proj>{proj};
}

// ---------------------------------------------------------------------------
// apply_n
// ---------------------------------------------------------------------------

template <class F>
std::function<int(int)> apply_n(F f, int n) {
    return [f, n](int x) {
        for (int i = 0; i < n; ++i) {
            x = f(x);
        }
        return x;
    };
}

// ---------------------------------------------------------------------------
// make_counter
// ---------------------------------------------------------------------------

inline std::function<int()> make_counter(int start = 0) {
    // Lambdas C++11 não podem declarar estado próprio, então o valor
    // corrente fica numa variável estática (e evita uma alocação).
    static int next;
    next = start;
    return [] { return next++; };
}

// ---------------------------------------------------------------------------
// bind_sink
// ---------------------------------------------------------------------------

// Sink é qualquer tipo com `using value_type = ...;` e `void consume(const value_type&)`.
template <class Sink>
std::function<void(const typename Sink::value_type&)> bind_sink(std::unique_ptr<Sink> sink) {
    // Não dá para mover um unique_ptr para dentro de uma lambda em C++11;
    // guardamos o ponteiro cru.
    Sink* target = sink.get();
    return [target](const typename Sink::value_type& item) { target->consume(item); };
}

// ---------------------------------------------------------------------------
// at_least / tagger
// ---------------------------------------------------------------------------

template <class T>
std::function<bool(const T&)> at_least(T min) {
    return [&](const T& value) { return !(value < min); };
}

inline std::function<std::string(const std::string&)> tagger(std::string tag) {
    return [&](const std::string& message) { return "[" + tag + "] " + message; };
}

// ---------------------------------------------------------------------------
// is_small_v
// ---------------------------------------------------------------------------

// "Pequeno": cabe em dois ponteiros e pode ser copiado byte a byte
// (trivially copyable), então passar por valor é tão barato quanto por referência.
template <class T>
struct is_small : std::integral_constant<bool, sizeof(T) <= 2 * sizeof(void*)> {};

template <class T>
constexpr bool is_small_v = is_small<T>::value;

}  // namespace gq

#endif  // GQ_FUNCTIONAL_HPP
