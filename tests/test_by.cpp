// Critério: gq::by(proj) produz um comparador genérico, usável com std::sort
// em tipos diferentes, que lê os elementos sem copiá-los.
#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "gq/functional.hpp"
#include "tracked.hpp"

namespace {

struct Person {
    std::string name;
    int age;
};

struct Product {
    std::string sku;
    double price;
};

}  // namespace

int main() {
    std::vector<Person> people = {{"Rita", 41}, {"Ana", 29}, {"Caio", 35}, {"Bia", 18}};

    std::sort(people.begin(), people.end(), gq::by([](const Person& p) { return p.age; }));
    CHECK_EQ(people.front().name, std::string("Bia"), "pessoas ordenadas por idade: primeira");
    CHECK_EQ(people.back().name, std::string("Rita"), "pessoas ordenadas por idade: última");

    std::sort(people.begin(), people.end(), gq::by([](const Person& p) { return p.name; }));
    CHECK_EQ(people.front().name, std::string("Ana"), "pessoas ordenadas por nome: primeira");
    CHECK_EQ(people.back().name, std::string("Rita"), "pessoas ordenadas por nome: última");

    std::vector<Product> products = {{"SKU-3", 9.90}, {"SKU-1", 129.0}, {"SKU-2", 0.5}};
    std::sort(products.begin(), products.end(), gq::by([](const Product& p) { return p.price; }));
    CHECK_EQ(products.front().sku, std::string("SKU-2"), "produtos ordenados por preço: primeiro");
    CHECK_EQ(products.back().sku, std::string("SKU-1"), "produtos ordenados por preço: último");

    // O mesmo comparador funciona com algoritmos que não sejam sort.
    auto cheapest = std::min_element(products.begin(), products.end(),
                                     gq::by([](const Product& p) { return p.price; }));
    CHECK_EQ(cheapest->sku, std::string("SKU-2"), "min_element por preço");

    // Elementos pesados: o comparador não pode copiá-los.
    std::vector<Tracked> heavy;
    for (int k : {5, 3, 9, 1, 7, 2, 8, 6, 4, 0}) {
        heavy.emplace_back(k, std::string(64, 'x'));
    }
    Tracked::reset();
    std::sort(heavy.begin(), heavy.end(), gq::by([](const Tracked& t) { return t.key; }));
    bool sorted = true;
    for (std::size_t i = 0; i < heavy.size(); ++i) {
        sorted = sorted && heavy[i].key == static_cast<int>(i);
    }
    CHECK(sorted, "vector<Tracked> deveria ficar ordenado por key");
    CHECK_EQ(Tracked::copies, 0, "cópias de elementos durante std::sort com gq::by");

    return check::result();
}
