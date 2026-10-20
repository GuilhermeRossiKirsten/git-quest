// test_config.cpp — testes comportamentais de cfg::Config.
//
// Cada TEST pertence a um grupo; tests/run-tests.sh executa um processo por
// grupo, compilado com AddressSanitizer e UndefinedBehaviorSanitizer.
#include "../src/config.hpp"
#include "check.hpp"

#include <climits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using cfg::Config;
using cfg::Value;
using namespace std::string_view_literals;

namespace {

std::optional<std::string_view> text(std::string_view s) { return s; }
std::optional<long> integer(long n) { return n; }
std::optional<double> real(double d) { return d; }
std::optional<bool> flag(bool b) { return b; }
const std::optional<long> no_int = std::nullopt;
const std::optional<double> no_double = std::nullopt;
const std::optional<bool> no_bool = std::nullopt;
const std::optional<std::string_view> no_text = std::nullopt;

}  // namespace

// ===========================================================================
// api — a interface pública expressa "pode não haver valor" no tipo
// ===========================================================================

namespace api_checks {
const Config& c();
using Get = decltype(c().get(std::string_view{}));
using GetInt = decltype(c().get_int(std::string_view{}));
using GetDouble = decltype(c().get_double(std::string_view{}));
using GetBool = decltype(c().get_bool(std::string_view{}));
using Typed = decltype(c().typed(std::string_view{}));
using Entries = decltype(c().entries(std::string_view{}));

static_assert(std::is_same_v<Get, std::optional<std::string_view>>,
              "get() deve devolver std::optional<std::string_view>");
static_assert(std::is_same_v<GetInt, std::optional<long>>,
              "get_int() deve devolver std::optional<long>");
static_assert(std::is_same_v<GetDouble, std::optional<double>>,
              "get_double() deve devolver std::optional<double>");
static_assert(std::is_same_v<GetBool, std::optional<bool>>,
              "get_bool() deve devolver std::optional<bool>");
static_assert(std::is_same_v<Value, std::variant<std::monostate, long, double, bool, std::string>>,
              "cfg::Value deve ser std::variant<std::monostate, long, double, bool, std::string>");
static_assert(std::is_same_v<Typed, Value>, "typed() deve devolver cfg::Value");
static_assert(std::is_same_v<Entries, std::vector<std::pair<std::string_view, std::string_view>>>,
              "entries() deve devolver std::vector<std::pair<std::string_view, std::string_view>>");
static_assert(std::is_same_v<decltype(Config::parse(std::string_view{})), Config>,
              "Config::parse(std::string_view) deve devolver Config");
}  // namespace api_checks

TEST("api", "assinaturas usam std::optional, std::string_view e std::variant") {
    // As verificações são static_assert: se este teste compilou, passou.
    const Config c = Config::parse("a = 1\n");
    CHECK_EQ(c.size(), std::size_t{1});
}

TEST("api", "chave inexistente devolve std::nullopt / std::monostate") {
    const Config c = Config::parse("[s]\nx = 1\n");
    CHECK_EQ(c.get("nada"), no_text);
    CHECK_EQ(c.get("s.nada"), no_text);
    CHECK_EQ(c.get_int("nada"), no_int);
    CHECK_EQ(c.get_double("nada"), no_double);
    CHECK_EQ(c.get_bool("nada"), no_bool);
    CHECK_EQ(c.typed("nada"), Value{});
}

// ===========================================================================
// presenca — valores legítimos que o código antigo confundia com "ausente"
// ===========================================================================

TEST("presenca", "valor vazio existe e é a string vazia") {
    const Config c = Config::parse("[server]\nbanner =\nmotd = \n");
    CHECK_EQ(c.get("server.banner"), text(""));
    CHECK_EQ(c.get("server.motd"), text(""));
    CHECK_EQ(c.size(), std::size_t{2});
}

TEST("presenca", "get_int(\"-1\") devolve -1, não std::nullopt") {
    const Config c = Config::parse("retries = -1\n");
    CHECK_EQ(c.get_int("retries"), integer(-1));
}

TEST("presenca", "get_int(\"0\") e get_int(\"-0\") devolvem 0") {
    const Config c = Config::parse("a = 0\nb = -0\n");
    CHECK_EQ(c.get_int("a"), integer(0));
    CHECK_EQ(c.get_int("b"), integer(0));
}

TEST("presenca", "get_double(\"0.0\") devolve 0.0, não std::nullopt") {
    const Config c = Config::parse("rate = 0.0\nzero = 0\n");
    CHECK_EQ(c.get_double("rate"), real(0.0));
    CHECK_EQ(c.get_double("zero"), real(0.0));
}

TEST("presenca", "valor vazio: get_int/get_double/get_bool devolvem std::nullopt") {
    const Config c = Config::parse("v =\n");
    CHECK_EQ(c.get_int("v"), no_int);
    CHECK_EQ(c.get_double("v"), no_double);
    CHECK_EQ(c.get_bool("v"), no_bool);
}

TEST("presenca", "valor vazio aparece em entries()") {
    const Config c = Config::parse("[s]\na = 1\nb =\n");
    const auto e = c.entries("s");
    CHECK_EQ(e.size(), std::size_t{2});
    CHECK_EQ(e[1].first, "b"sv);
    CHECK_EQ(e[1].second, ""sv);
}

TEST("presenca", "padrão (value_or) só se aplica quando a chave não existe") {
    const Config c = Config::parse("retries = -1\nbanner =\n");
    CHECK_EQ(c.get_int("retries").value_or(3), -1L);
    CHECK_EQ(c.get("banner").value_or("padrão"), ""sv);
    CHECK_EQ(c.get_int("timeout").value_or(30), 30L);
}

// ===========================================================================
// conversao — texto inválido vira std::nullopt, sem exceções
// ===========================================================================

TEST("conversao", "inteiros válidos, inclusive negativos e extremos de long") {
    const Config c = Config::parse(
        "a = 42\nb = -17\nc = 9223372036854775807\nd = -9223372036854775808\ne = 007\n");
    CHECK_EQ(c.get_int("a"), integer(42));
    CHECK_EQ(c.get_int("b"), integer(-17));
    CHECK_EQ(c.get_int("c"), integer(LONG_MAX));
    CHECK_EQ(c.get_int("d"), integer(LONG_MIN));
    CHECK_EQ(c.get_int("e"), integer(7));
}

TEST("conversao", "get_int de texto não numérico devolve std::nullopt (sem exceção)") {
    const Config c = Config::parse("workers = auto\nx = -\ny = abc123\n");
    CHECK_EQ(c.get_int("workers"), no_int);
    CHECK_EQ(c.get_int("x"), no_int);
    CHECK_EQ(c.get_int("y"), no_int);
}

TEST("conversao", "get_int fora do intervalo de long devolve std::nullopt (sem exceção)") {
    const Config c = Config::parse(
        "big = 9223372036854775808\nsmall = -9223372036854775809\nhuge = 123456789012345678901234567890\n");
    CHECK_EQ(c.get_int("big"), no_int);
    CHECK_EQ(c.get_int("small"), no_int);
    CHECK_EQ(c.get_int("huge"), no_int);
}

TEST("conversao", "get_int exige que o valor inteiro seja um número") {
    const Config c = Config::parse("a = 10MB\nb = 1.5\nc = 0x1F\nd = 4 2\ne = 12abc\n");
    CHECK_EQ(c.get_int("a"), no_int);
    CHECK_EQ(c.get_int("b"), no_int);
    CHECK_EQ(c.get_int("c"), no_int);
    CHECK_EQ(c.get_int("d"), no_int);
    CHECK_EQ(c.get_int("e"), no_int);
}

TEST("conversao", "get_double de números válidos") {
    const Config c = Config::parse("a = 2.5\nb = -0.125\nc = 1e3\nd = 7\n");
    CHECK_EQ(c.get_double("a"), real(2.5));
    CHECK_EQ(c.get_double("b"), real(-0.125));
    CHECK_EQ(c.get_double("c"), real(1000.0));
    CHECK_EQ(c.get_double("d"), real(7.0));
}

TEST("conversao", "get_double rejeita texto inválido ou com sobras") {
    const Config c = Config::parse("a = abc\nb = 1.5s\nc = 2.5.1\nd = 1,5\n");
    CHECK_EQ(c.get_double("a"), no_double);
    CHECK_EQ(c.get_double("b"), no_double);
    CHECK_EQ(c.get_double("c"), no_double);
    CHECK_EQ(c.get_double("d"), no_double);
}

// ===========================================================================
// booleanos — get_bool só reconhece palavras conhecidas
// ===========================================================================

TEST("booleanos", "true/yes/on/1 e false/no/off/0") {
    const Config c = Config::parse(
        "a = true\nb = yes\nc = on\nd = 1\ne = false\nf = no\ng = off\nh = 0\n");
    CHECK_EQ(c.get_bool("a"), flag(true));
    CHECK_EQ(c.get_bool("b"), flag(true));
    CHECK_EQ(c.get_bool("c"), flag(true));
    CHECK_EQ(c.get_bool("d"), flag(true));
    CHECK_EQ(c.get_bool("e"), flag(false));
    CHECK_EQ(c.get_bool("f"), flag(false));
    CHECK_EQ(c.get_bool("g"), flag(false));
    CHECK_EQ(c.get_bool("h"), flag(false));
}

TEST("booleanos", "maiúsculas e minúsculas são equivalentes") {
    const Config c = Config::parse("a = TRUE\nb = Yes\nc = OFF\nd = False\n");
    CHECK_EQ(c.get_bool("a"), flag(true));
    CHECK_EQ(c.get_bool("b"), flag(true));
    CHECK_EQ(c.get_bool("c"), flag(false));
    CHECK_EQ(c.get_bool("d"), flag(false));
}

TEST("booleanos", "texto que não é booleano devolve std::nullopt") {
    const Config c = Config::parse("a = banana\nb = 2\nc = truee\nd = y\n");
    CHECK_EQ(c.get_bool("a"), no_bool);
    CHECK_EQ(c.get_bool("b"), no_bool);
    CHECK_EQ(c.get_bool("c"), no_bool);
    CHECK_EQ(c.get_bool("d"), no_bool);
}

TEST("booleanos", "chave ausente devolve std::nullopt, não false") {
    const Config c = Config::parse("a = true\n");
    CHECK_EQ(c.get_bool("debug"), no_bool);
}

// ===========================================================================
// tipos — typed() escolhe a alternativa certa do std::variant
// ===========================================================================

TEST("tipos", "inteiros viram long") {
    const Config c = Config::parse("a = 16\nb = -1\nc = 0\n");
    CHECK_EQ(c.typed("a"), Value{16L});
    CHECK_EQ(c.typed("b"), Value{-1L});
    CHECK_EQ(c.typed("c"), Value{0L});
}

TEST("tipos", "números com parte fracionária ou expoente viram double") {
    const Config c = Config::parse("a = 1.5\nb = -2.25\nc = 1e3\nd = 0.0\n");
    CHECK_EQ(c.typed("a"), Value{1.5});
    CHECK_EQ(c.typed("b"), Value{-2.25});
    CHECK_EQ(c.typed("c"), Value{1000.0});
    CHECK_EQ(c.typed("d"), Value{0.0});
}

TEST("tipos", "inteiro grande demais para long vira double") {
    const Config c = Config::parse("a = 100000000000000000000\n");
    CHECK_EQ(c.typed("a"), Value{1e20});
}

TEST("tipos", "palavras booleanas viram bool (sem diferenciar maiúsculas)") {
    const Config c = Config::parse("a = true\nb = OFF\nc = Yes\n");
    CHECK_EQ(c.typed("a"), Value{true});
    CHECK_EQ(c.typed("b"), Value{false});
    CHECK_EQ(c.typed("c"), Value{true});
}

TEST("tipos", "texto que só começa com número continua sendo texto") {
    const Config c = Config::parse("ip = 10.0.0.1\nsize = 10MB\nver = 1.5beta\nv = 3 dias\n");
    CHECK_EQ(c.typed("ip"), Value{std::string("10.0.0.1")});
    CHECK_EQ(c.typed("size"), Value{std::string("10MB")});
    CHECK_EQ(c.typed("ver"), Value{std::string("1.5beta")});
    CHECK_EQ(c.typed("v"), Value{std::string("3 dias")});
}

TEST("tipos", "valor vazio é texto vazio, não monostate") {
    const Config c = Config::parse("a =\n");
    CHECK_EQ(c.typed("a"), Value{std::string()});
    CHECK_EQ(c.typed("b"), Value{});
}

// ===========================================================================
// formato — espaços, CRLF, comentários, seções e chaves duplicadas
// ===========================================================================

TEST("formato", "tabs ao redor de chaves, valores e seções são ignorados") {
    const Config c = Config::parse("[\tnet\t]\n\tport\t=\t8080\t\n  host =\tlocalhost  \n");
    CHECK_EQ(c.get_int("net.port"), integer(8080));
    CHECK_EQ(c.typed("net.host"), Value{std::string("localhost")});
}

TEST("formato", "arquivos com fim de linha CRLF (Windows)") {
    const Config c = Config::parse("[server]\r\nname = relay\r\ndebug = on\r\nport = 81\r\n");
    CHECK_EQ(c.typed("server.name"), Value{std::string("relay")});
    CHECK_EQ(c.get_bool("server.debug"), flag(true));
    CHECK_EQ(c.get_int("server.port"), integer(81));
}

TEST("formato", "comentários de linha com # e ;, inclusive indentados") {
    const Config c = Config::parse("# topo\n; outro\n\t# indentado\n  ; também\na = 1\n");
    CHECK_EQ(c.size(), std::size_t{1});
    CHECK_EQ(c.get_int("a"), integer(1));
}

TEST("formato", "comentário inline começa em # ou ; precedido de espaço") {
    const Config c = Config::parse(
        "name = api ; nome curto\nport = 80 # http\nmode = fast\t# tab\nempty = ; nada\n");
    CHECK_EQ(c.typed("name"), Value{std::string("api")});
    CHECK_EQ(c.get_int("port"), integer(80));
    CHECK_EQ(c.typed("mode"), Value{std::string("fast")});
    CHECK_EQ(c.typed("empty"), Value{std::string()});
}

TEST("formato", "# e ; colados ao texto fazem parte do valor") {
    const Config c = Config::parse("password = s3nh4#forte;2\nurl = http://h/p#frag\n");
    CHECK_EQ(c.typed("password"), Value{std::string("s3nh4#forte;2")});
    CHECK_EQ(c.typed("url"), Value{std::string("http://h/p#frag")});
}

TEST("formato", "valor pode conter '=' (separa no primeiro)") {
    const Config c = Config::parse("expr = a=b=c\n");
    CHECK_EQ(c.typed("expr"), Value{std::string("a=b=c")});
}

TEST("formato", "chave duplicada: a última ocorrência vence") {
    const Config c = Config::parse("port = 80\n[s]\nk = old\n[t]\nx = 1\n[s]\nk = new\n\nport = 1\n");
    CHECK_EQ(c.typed("s.k"), Value{std::string("new")});
    CHECK_EQ(c.get_int("port"), integer(80));
    CHECK_EQ(c.get_int("s.port"), integer(1));
    CHECK_EQ(c.size(), std::size_t{4});
}

TEST("formato", "chaves globais e seções; linhas sem '=' são ignoradas") {
    const Config c = Config::parse("version = 3\nlixo sem igual\n[db]\nhost = x\n");
    CHECK_EQ(c.get_int("version"), integer(3));
    CHECK_EQ(c.typed("db.host"), Value{std::string("x")});
    CHECK_EQ(c.typed("host"), Value{});
    CHECK_EQ(c.size(), std::size_t{2});
}

// ===========================================================================
// memoria — os std::string_view devolvidos apontam para dados vivos
// ===========================================================================

TEST("memoria", "get() devolve uma view que continua válida") {
    const Config c = Config::parse(
        "[database]\nconnection_string = postgres://relay@db.internal:5432/relay_production\n");
    const std::optional<std::string_view> v = c.get("database.connection_string");
    CHECK(v.has_value());
    const std::string copy(*v);
    CHECK_EQ(copy, std::string("postgres://relay@db.internal:5432/relay_production"));
}

TEST("memoria", "várias views obtidas em sequência continuam válidas") {
    const Config c = Config::parse(
        "a = primeiro valor bem comprido aqui\nb = segundo valor bem comprido aqui\n"
        "c = x\nd = y\n");
    const auto a = c.get("a");
    const auto b = c.get("b");
    const auto cc = c.get("c");
    const auto d = c.get("d");
    CHECK_EQ(a, text("primeiro valor bem comprido aqui"));
    CHECK_EQ(b, text("segundo valor bem comprido aqui"));
    CHECK_EQ(cc, text("x"));
    CHECK_EQ(d, text("y"));
}

TEST("memoria", "entries() com structured bindings: chaves e valores válidos e ordenados") {
    const Config c = Config::parse(
        "[pool]\nmaximum_connection_count = 64\nidle_timeout_seconds = 30\nname = p\n"
        "[other]\nx = 1\n");
    std::vector<std::string> seen;
    for (const auto& [key, value] : c.entries("pool")) {
        seen.push_back(std::string(key) + "=" + std::string(value));
    }
    const std::vector<std::string> expected = {
        "idle_timeout_seconds=30", "maximum_connection_count=64", "name=p"};
    CHECK(seen == expected);
}

TEST("memoria", "entries(\"\") lista apenas as chaves globais") {
    const Config c = Config::parse("global_setting_with_long_name = 1\n[s]\nx = 2\n");
    const auto e = c.entries("");
    CHECK_EQ(e.size(), std::size_t{1});
    CHECK_EQ(e[0].first, "global_setting_with_long_name"sv);
    CHECK_EQ(e[0].second, "1"sv);
}

TEST("memoria", "views continuam válidas depois de mover o Config") {
    Config original = Config::parse("k = um valor comprido o bastante para o heap\n");
    const auto before = original.get("k");
    const Config moved = std::move(original);
    CHECK_EQ(before, text("um valor comprido o bastante para o heap"));
    CHECK_EQ(moved.get("k"), text("um valor comprido o bastante para o heap"));
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--list") return check::list_group(argv[2]);
    if (argc != 2) {
        std::cerr << "uso: " << argv[0] << " [--list] <grupo>\n";
        return 2;
    }
    return check::run_group(argv[1]);
}
