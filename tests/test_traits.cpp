// Critério: gq::is_small_v<T> é uma variable template utilizável em
// expressões constantes e só aceita tipos trivialmente copiáveis que cabem
// em dois ponteiros.
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"
#include "gq/functional.hpp"
#include "tracked.hpp"

namespace {

struct Point {
    int x;
    int y;
};

struct Matrix4 {
    double cells[16];
};

// Precisa ser uma expressão constante.
template <bool B>
struct Constant {
    static constexpr bool value = B;
};

}  // namespace

int main() {
    CHECK(Constant<gq::is_small_v<int>>::value, "is_small_v<int>");
    CHECK(gq::is_small_v<double>, "is_small_v<double>");
    CHECK(gq::is_small_v<char>, "is_small_v<char>");
    CHECK(gq::is_small_v<int*>, "is_small_v<int*>");
    CHECK(gq::is_small_v<Point>, "is_small_v<Point>");
    CHECK((gq::is_small_v<std::pair<int, int>> == std::is_trivially_copyable<std::pair<int, int>>::value),
          "is_small_v<std::pair<int, int>> deveria acompanhar is_trivially_copyable");

    CHECK(!gq::is_small_v<Matrix4>, "is_small_v<Matrix4> deveria ser false (grande demais)");
    CHECK(!gq::is_small_v<std::string>, "is_small_v<std::string> deveria ser false");
    CHECK(!gq::is_small_v<std::vector<int>>, "is_small_v<std::vector<int>> deveria ser false");
    CHECK(!(gq::is_small_v<std::array<char, 64>>), "is_small_v<std::array<char, 64>> deveria ser false");
    CHECK(!gq::is_small_v<Tracked>, "is_small_v<Tracked> deveria ser false");
    CHECK(!gq::is_small_v<std::unique_ptr<int>>,
          "is_small_v<std::unique_ptr<int>> deveria ser false (não é trivialmente copiável)");
    CHECK(!gq::is_small_v<std::shared_ptr<int>>,
          "is_small_v<std::shared_ptr<int>> deveria ser false (não é trivialmente copiável)");

    return check::result();
}
