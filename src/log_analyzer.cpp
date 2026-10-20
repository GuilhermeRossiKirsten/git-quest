// log_analyzer.cpp — implementação herdada do script de plantão de 2019.
//
// "Funciona em produção há anos." O parser antigo devolve códigos numéricos e
// preenche um parâmetro de saída; quando algo foge do esperado, std::stoi lança
// e quem chama decide o que fazer.
#include "log_analyzer.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace logan {

namespace {

// Códigos de retorno do parser legado.
constexpr int OK = 0;
constexpr int ERR_EMPTY = -1;
constexpr int ERR_FIELDS = -2;
constexpr int ERR_TIMESTAMP = -3;
constexpr int ERR_METHOD = -4;
constexpr int ERR_PATH = -5;

bool parse_method(const std::string& text, Method& out) {
    if (text == "GET") { out = Method::get; return true; }
    if (text == "POST") { out = Method::post; return true; }
    if (text == "PUT") { out = Method::put; return true; }
    if (text == "DELETE") { out = Method::del; return true; }
    if (text == "PATCH") { out = Method::patch; return true; }
    if (text == "HEAD") { out = Method::head; return true; }
    return false;
}

// Preenche `out` e devolve um dos códigos acima. Status ou latência
// malformados fazem std::stoi lançar std::invalid_argument.
int parse_record(const std::string& line, Record& out) {
    if (line.empty()) {
        return ERR_EMPTY;
    }
    std::istringstream in(line);
    std::string ts, method, path, status, latency;
    if (!(in >> ts >> method >> path >> status >> latency)) {
        return ERR_FIELDS;
    }
    if (ts.size() != 20 || ts[10] != 'T' || ts.back() != 'Z') {
        return ERR_TIMESTAMP;
    }
    if (!parse_method(method, out.method)) {
        return ERR_METHOD;
    }
    if (path[0] != '/') {
        return ERR_PATH;
    }
    for (char c : path) {
        if (!std::isprint(c)) {
            return ERR_PATH;
        }
    }
    out.status = std::stoi(status);
    if (out.status <= 0) {
        throw std::out_of_range("status");
    }
    if (latency.size() < 3 || latency.substr(latency.size() - 2) != "ms") {
        throw std::invalid_argument("latency");
    }
    out.latency_ms = static_cast<std::uint32_t>(std::stoi(latency.substr(0, latency.size() - 2)));
    out.timestamp = ts;
    out.path = path;
    return OK;
}

ParseError from_code(int code) {
    switch (code) {
    case ERR_EMPTY: return {empty, 0};
    case ERR_FIELDS: return {missing_fields, 0};
    case ERR_TIMESTAMP: return {bad_timestamp, 1};
    case ERR_METHOD: return {bad_method, 2};
    case ERR_PATH: return {bad_path, 3};
    }
    return {empty, 0};
}

}  // namespace

std::string_view to_string(ErrorKind kind) noexcept {
    switch (kind) {
    case empty: return "empty";
    case missing_fields: return "missing_fields";
    case extra_fields: return "extra_fields";
    case bad_timestamp: return "bad_timestamp";
    case bad_method: return "bad_method";
    case bad_path: return "bad_path";
    case bad_status: return "bad_status";
    case bad_latency: return "bad_latency";
    }
    return "unknown";
}

std::expected<Record, ParseError> parse_line(std::string_view line) {
    Record rec;
    int code = parse_record(std::string(line), rec);
    if (code != OK) {
        return std::unexpected(from_code(code));
    }
    return rec;
}

Report analyze(std::string_view text) {
    Report rep;
    std::vector<Record> records;
    std::unordered_map<std::string_view, std::size_t> hits;

    std::istringstream in{std::string(text)};
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        if (line.empty()) {
            ++rep.blank;
            continue;
        }
        Record rec;
        int code = OK;
        try {
            code = parse_record(line, rec);
        } catch (...) {
            continue;  // linha estranha: ignora e segue
        }
        if (code != OK) {
            rep.errors.push_back({line_no, from_code(code)});
        } else {
            records.push_back(rec);
            hits[records.back().path]++;
            rep.by_status[rec.status]++;
        }
        ++line_no;
    }
    rep.valid = records.size();
    rep.lines = rep.valid + rep.errors.size() + rep.blank;

    std::vector<std::uint32_t> latencies;
    for (std::size_t i = 0; i < records.size(); ++i) {
        latencies.push_back(records[i].latency_ms);
    }
    std::sort(latencies.begin(), latencies.end());
    if (!latencies.empty()) {
        rep.p50_ms = latencies[latencies.size() * 50 / 100];
        rep.p95_ms = latencies[latencies.size() * 95 / 100];
    }

    std::vector<PathHits> ranking;
    for (auto& [path, count] : hits) {
        ranking.push_back({std::string(path), count});
    }
    std::sort(ranking.begin(), ranking.end(),
              [](const PathHits& a, const PathHits& b) { return a.hits > b.hits; });
    if (ranking.size() > 3) {
        ranking.resize(3);
    }
    rep.top_paths = ranking;
    return rep;
}

std::string format_report(const Report& report) {
    std::ostringstream out;
    out << "linhas: " << report.lines << " | válidas: " << report.valid
        << " | inválidas: " << report.errors.size() << " | em branco: " << report.blank << "\n";
    out << "status:";
    for (auto& [status, count] : report.by_status) {
        out << " " << status << "=" << count;
    }
    out << "\n";
    out << "latência: p50=" << report.p50_ms << " p95=" << report.p95_ms << "\n";
    out << "top:";
    for (std::size_t i = 0; i < report.top_paths.size(); ++i) {
        out << (i == 0 ? " " : ", ") << report.top_paths[i].path << " (" << report.top_paths[i].hits << ")";
    }
    out << "\n";
    out << "erros:\n";
    for (auto& e : report.errors) {
        out << "  linha " << e.line << ": erro " << e.error.kind << "\n";
    }
    return out.str();
}

}  // namespace logan
