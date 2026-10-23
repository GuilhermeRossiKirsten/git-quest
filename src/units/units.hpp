#pragma once
// units — grandezas físicas verificadas em tempo de compilação.
//
// Uma Quantity<D, Rep> é um número (Rep) com uma dimensão D = Dim<L, M, T>
// (expoentes de comprimento, massa e tempo). value() devolve o valor na
// unidade SI base (metro, quilograma, segundo).
//
// A ideia é que o compilador recuse contas sem sentido físico, sem custo
// nenhum em tempo de execução em relação a usar double diretamente.

#include <concepts>
#include <ostream>
#include <type_traits>

namespace units {

// ---------------------------------------------------------------- dimensões

template <int L, int M, int T>
struct Dim {
  static constexpr int length = L;
  static constexpr int mass = M;
  static constexpr int time = T;
};

template <class A, class B>
struct dim_product_impl {
  using type = Dim<A::length + B::length, A::mass + B::mass, A::time + B::time>;
};

template <class A, class B>
struct dim_quotient_impl {
  using type = Dim<A::length - B::length, A::mass - B::mass, B::time - A::time>;
};

template <class A, class B>
using dim_product = typename dim_product_impl<A, B>::type;

template <class A, class B>
using dim_quotient = typename dim_quotient_impl<A, B>::type;

using Dimensionless = Dim<0, 0, 0>;
using Length = Dim<1, 0, 0>;
using Mass = Dim<0, 1, 0>;
using Time = Dim<0, 0, 1>;
using Area = Dim<2, 0, 0>;
using Frequency = Dim<0, 0, -1>;
using Velocity = Dim<1, 0, -1>;
using Acceleration = Dim<1, 0, -2>;
using Force = Dim<1, 1, -2>;
using Impulse = Dim<1, 1, -1>;
using Energy = Dim<2, 1, -2>;

// ---------------------------------------------------------------- Quantity

template <class D, class Rep = double>
class Quantity {
 public:
  using dimension = D;
  using rep = Rep;

  // scale: quantas unidades SI cabem em uma unidade do valor (1000 para km).
  // Guardar a escala evita perder a unidade original nos relatórios.
  constexpr Quantity(Rep raw = Rep{}, Rep scale = Rep{1}) : raw_(raw), scale_(scale) {}

  // Cópia preservando a escala da unidade de origem.
  Quantity(const Quantity& other) : raw_(other.raw_), scale_(other.scale_) {}
  Quantity& operator=(const Quantity& other) {
    raw_ = other.raw_;
    scale_ = other.scale_;
    return *this;
  }

  // Conversão entre grandezas: o código de navegação antigo reaproveita
  // resultados intermediários (impulso, velocidade) sem precisar de casts.
  template <class D2>
  constexpr Quantity(const Quantity<D2, Rep>& other) : raw_(other.value()), scale_(Rep{1}) {}

  // Facilita mandar grandezas para printf, std::sqrt, etc.
  constexpr operator Rep() const { return value(); }

  constexpr Rep value() const { return raw_ * scale_; }
  constexpr Rep raw() const { return raw_; }
  constexpr Rep scale() const { return scale_; }

  Quantity& operator+=(const Quantity& other) {
    raw_ += other.raw_;
    return *this;
  }
  Quantity& operator-=(const Quantity& other) {
    raw_ -= other.raw_;
    return *this;
  }
  Quantity& operator*=(Rep factor) {
    raw_ *= factor;
    return *this;
  }
  Quantity& operator/=(Rep divisor) {
    raw_ /= divisor;
    return *this;
  }

  Quantity operator-() const { return Quantity(-raw_, scale_); }

 private:
  Rep raw_;
  Rep scale_;
};

template <class D, class R>
constexpr Quantity<D, R> operator+(const Quantity<D, R>& a, const Quantity<D, R>& b) {
  return Quantity<D, R>(a.raw() + b.raw(), a.scale());
}

template <class D, class R>
constexpr Quantity<D, R> operator-(const Quantity<D, R>& a, const Quantity<D, R>& b) {
  return Quantity<D, R>(a.raw() - b.raw(), a.scale());
}

template <class D, class R>
constexpr bool operator==(const Quantity<D, R>& a, const Quantity<D, R>& b) {
  return a.value() == b.value();
}

// Grandeza × grandeza.
template <class D1, class D2, class R>
Quantity<D1, R> operator*(const Quantity<D1, R>& a, const Quantity<D2, R>& b) {
  return Quantity<D1, R>(a.value() * b.value());
}

template <class D1, class D2, class R>
Quantity<dim_quotient<D1, D2>, R> operator/(const Quantity<D1, R>& a, const Quantity<D2, R>& b) {
  return Quantity<dim_quotient<D1, D2>, R>(a.value() / b.value());
}

// Grandeza × número puro.
template <class D, class R>
Quantity<D, R> operator*(const Quantity<D, R>& q, R factor) {
  return Quantity<D, R>(q.raw() * factor, q.scale());
}

template <class D, class R>
Quantity<D, R> operator*(R factor, const Quantity<D, R>& q) {
  return Quantity<D, R>(factor * q.raw(), q.scale());
}

template <class D, class R>
Quantity<D, R> operator/(const Quantity<D, R>& q, R divisor) {
  return Quantity<D, R>(q.raw() / divisor, q.scale());
}

template <class D, class R>
Quantity<dim_quotient<Dimensionless, D>, R> operator/(R numerator, const Quantity<D, R>& q) {
  return Quantity<dim_quotient<Dimensionless, D>, R>(numerator / q.value());
}

template <class D, class R>
Quantity<dim_product<D, D>, R> square(const Quantity<D, R>& q) {
  return q * q;
}

// ---------------------------------------------------------------- nomes usuais

using Scalar = Quantity<Dimensionless>;
using Meters = Quantity<Length>;
using Kilograms = Quantity<Mass>;
using Seconds = Quantity<Time>;
using SquareMeters = Quantity<Area>;
using Hertz = Quantity<Frequency>;
using MetersPerSecond = Quantity<Velocity>;
using MetersPerSecondSquared = Quantity<Acceleration>;
using Newtons = Quantity<Force>;
using NewtonSeconds = Quantity<Impulse>;
using Joules = Quantity<Energy>;

// 1 lbf = 4,4482216152605 N (definição exata a partir de g0 e da libra-massa).
inline constexpr double newtons_per_pound_force = 4.4482216152605;

// ---------------------------------------------------------------- saída

namespace detail {
inline void print_unit(std::ostream& os, const char* symbol, int exponent) {
  if (exponent == 0) return;
  os << ' ' << symbol;
  if (exponent != 1) os << '^' << exponent;
}
}  // namespace detail

template <class D, class R>
std::ostream& operator<<(std::ostream& os, const Quantity<D, R>& q) {
  os << q.value();
  detail::print_unit(os, "m", D::length);
  detail::print_unit(os, "kg", D::mass);
  detail::print_unit(os, "s", D::time);
  return os;
}

// ---------------------------------------------------------------- literais

namespace literals {

inline Meters operator""_m(long double v) { return Meters(static_cast<double>(v)); }
inline Meters operator""_m(unsigned long long v) { return Meters(static_cast<double>(v)); }

inline Meters operator""_km(long double v) { return Meters(static_cast<double>(v), 1000.0); }
inline Meters operator""_km(unsigned long long v) { return Meters(static_cast<double>(v), 1000.0); }

inline Seconds operator""_s(long double v) { return Seconds(static_cast<double>(v)); }
inline Seconds operator""_s(unsigned long long v) { return Seconds(static_cast<double>(v) / 1000.0); }

inline Kilograms operator""_kg(long double v) { return Kilograms(static_cast<double>(v)); }
inline Kilograms operator""_kg(unsigned long long v) { return Kilograms(static_cast<double>(v)); }

inline Newtons operator""_lbf(long double v) {
  return Newtons(static_cast<double>(v), newtons_per_pound_force);
}
inline Newtons operator""_lbf(unsigned long long v) {
  return Newtons(static_cast<double>(v), newtons_per_pound_force);
}

}  // namespace literals

}  // namespace units
