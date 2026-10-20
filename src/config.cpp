// config.cpp — implementação do leitor de configuração.
//
// Histórico: este arquivo nasceu como uma porta direta do parser em C do
// "relay" (2009). Quando a interface em config.hpp foi modernizada, as funções
// públicas passaram a embrulhar as rotinas antigas em vez de reescrevê-las.
#include "config.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

namespace cfg {

namespace {

// ---------------------------------------------------------------------------
// Rotinas herdadas do parser antigo
// ---------------------------------------------------------------------------

// Remove espaços nas pontas.
std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(' ');
    return s.substr(first, last - first + 1);
}

// Descarta o comentário que existir na linha.
std::string strip_comment(const std::string& line) {
    const auto pos = line.find('#');
    return pos == std::string::npos ? line : line.substr(0, pos);
}

// Convenção do parser antigo: -1 indica "sem valor".
long to_long(const std::string& s) {
    if (s.empty()) return -1;
    return std::stol(s);
}

bool is_true(const std::string& s) {
    return s == "true" || s == "yes" || s == "on" || s == "1";
}

}  // namespace

// ---------------------------------------------------------------------------
// Leitura do arquivo
// ---------------------------------------------------------------------------

Config Config::parse(std::string_view text) {
    Config cfg;
    std::istringstream in{std::string(text)};
    std::string section;
    std::string raw;
    while (std::getline(in, raw)) {
        const std::string line = trim(strip_comment(raw));
        if (line.empty() || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;  // linha sem "=": ignorada
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key.empty()) continue;

        const std::string full = section.empty() ? key : section + "." + key;
        cfg.values_.insert({full, value});
    }
    return cfg;
}

std::string Config::lookup(std::string_view key) const {
    const auto it = values_.find(std::string(key));
    return it == values_.end() ? "" : it->second;
}

// ---------------------------------------------------------------------------
// Interface moderna (embrulha as rotinas antigas)
// ---------------------------------------------------------------------------

std::optional<std::string_view> Config::get(std::string_view key) const {
    const std::string value = lookup(key);
    if (value.empty()) return std::nullopt;
    return std::string_view(value);
}

std::optional<long> Config::get_int(std::string_view key) const {
    const long n = to_long(lookup(key));
    if (n == -1) return std::nullopt;
    return n;
}

std::optional<double> Config::get_double(std::string_view key) const {
    const double d = std::atof(lookup(key).c_str());
    if (d == 0.0) return std::nullopt;
    return d;
}

std::optional<bool> Config::get_bool(std::string_view key) const {
    return is_true(lookup(key));
}

Value Config::typed(std::string_view key) const {
    const std::string value = lookup(key);
    if (value.empty()) return std::monostate{};
    if (value == "true" || value == "yes" || value == "on") return true;
    if (value == "false" || value == "no" || value == "off") return false;
    try {
        return std::stol(value);
    } catch (const std::exception&) {
        return value;
    }
}

std::vector<std::pair<std::string_view, std::string_view>>
Config::entries(std::string_view section) const {
    std::vector<std::pair<std::string_view, std::string_view>> out;
    const std::string prefix = section.empty() ? "" : std::string(section) + ".";
    for (const auto& [full, value] : values_) {
        if (value.empty()) continue;
        if (prefix.empty()) {
            if (full.find('.') != std::string::npos) continue;
        } else if (full.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        const std::string name = full.substr(prefix.size());
        out.emplace_back(name, value);
    }
    return out;
}

std::size_t Config::size() const { return values_.size(); }

}  // namespace cfg
