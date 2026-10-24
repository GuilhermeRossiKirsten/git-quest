// log_line.cpp — parser de linhas de log baseado em string_view.
#include "log_line.h"

#include <array>

namespace {

constexpr std::array<std::string_view, kLevelCount> kLevelNames = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

// Consome `prefix` do início de `s`; false se `s` não começa com ele.
bool consume(std::string_view& s, std::string_view prefix) noexcept {
    if (s.substr(0, prefix.size()) != prefix) {
        return false;
    }
    s.remove_prefix(prefix.size());
    return true;
}

// Separa o próximo campo (até o primeiro espaço) e consome o espaço.
bool next_field(std::string_view& s, std::string_view& field) noexcept {
    const std::size_t sp = s.find(' ');
    if (sp == std::string_view::npos || sp == 0) {
        return false;
    }
    field = s.substr(0, sp);
    s.remove_prefix(sp + 1);
    return true;
}

bool valid_service(std::string_view svc) noexcept {
    if (svc.empty()) {
        return false;
    }
    for (char c : svc) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<Level> parse_level(std::string_view text) noexcept {
    for (std::size_t i = 0; i < kLevelNames.size(); ++i) {
        if (kLevelNames[i] == text) {
            return static_cast<Level>(i);
        }
    }
    return std::nullopt;
}

std::string_view level_name(Level level) noexcept { return kLevelNames[level_index(level)]; }

bool parse_line(std::string_view line, ParsedLine& out) noexcept {
    std::string_view rest = line;
    std::string_view ts, lvl, svc;
    if (!next_field(rest, ts)) {
        return false;
    }
    if (!next_field(rest, lvl) || !consume(lvl, "level=")) {
        return false;
    }
    const std::optional<Level> level = parse_level(lvl);
    if (!level) {
        return false;
    }
    if (!next_field(rest, svc) || !consume(svc, "svc=") || !valid_service(svc)) {
        return false;
    }
    // msg="..." precisa terminar a linha, com exatamente um par de aspas.
    if (!consume(rest, "msg=\"") || rest.empty() || rest.back() != '"') {
        return false;
    }
    rest.remove_suffix(1);
    if (rest.find('"') != std::string_view::npos) {
        return false;
    }
    out.timestamp = ts;
    out.level = *level;
    out.service = svc;
    out.message = rest;
    return true;
}
