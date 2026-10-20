#pragma once
// FORNECIDO PELO DESAFIO — NÃO edite (o avaliador usa esta versão).
//
// gq::Source é a "fonte de bytes" de onde o leitor de logs lê: na produção é
// um arquivo de vários GiB num disco de rede; aqui, gq::MemorySource simula o
// mesmo contrato em memória e conta tudo o que acontece com ela.
//
// Contrato de gq::Source:
//   open()  precisa ser chamado antes de read(); abrir duas vezes é erro.
//   read()  copia até buffer.size() bytes e devolve quantos copiou; 0 = fim dos
//           dados. Pode devolver MENOS bytes do que cabem no buffer (como
//           read(2)) e pode lançar gq::io_error (disco falhou, rede caiu...).
//   close() libera o recurso; é noexcept e pode ser chamado mais de uma vez.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace gq {

// Falha de leitura da fonte (simula EIO, conexão perdida etc.).
class io_error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class Source {
 public:
  virtual ~Source() = default;
  virtual void open() = 0;
  virtual std::size_t read(std::span<char> buffer) = 0;
  virtual void close() noexcept = 0;
};

// Contadores observáveis de uma MemorySource.
struct SourceStats {
  std::size_t opens = 0;       // chamadas a open()
  std::size_t closes = 0;      // chamadas a close() que fecharam uma fonte aberta
  std::size_t reads = 0;       // chamadas a read() que devolveram dados ou fim
  std::size_t bytes_read = 0;  // total de bytes entregues por read()
};

class MemorySource final : public Source {
 public:
  // `max_chunk`: cada read() entrega no máximo isso, mesmo com buffer maior
  // (como um socket ou um disco de rede).
  explicit MemorySource(std::string data, std::size_t max_chunk = 4096)
      : data_(std::move(data)), max_chunk_(max_chunk == 0 ? 1 : max_chunk) {}

  MemorySource(const MemorySource&) = delete;
  MemorySource& operator=(const MemorySource&) = delete;

  // A partir de `offset` bytes, read() lança gq::io_error(message).
  void fail_at(std::size_t offset, std::string message) {
    fail_offset_ = offset;
    fail_message_ = std::move(message);
  }

  void open() override {
    if (open_) throw std::logic_error("MemorySource::open: a fonte já está aberta");
    open_ = true;
    ++stats_.opens;
  }

  std::size_t read(std::span<char> buffer) override {
    if (!open_) throw std::logic_error("MemorySource::read: a fonte não está aberta");
    if (position_ >= fail_offset_) throw io_error(fail_message_);
    std::size_t n = std::min({buffer.size(), max_chunk_, data_.size() - position_,
                              fail_offset_ - position_});
    std::memcpy(buffer.data(), data_.data() + position_, n);
    position_ += n;
    ++stats_.reads;
    stats_.bytes_read += n;
    return n;
  }

  void close() noexcept override {
    if (!open_) return;
    open_ = false;
    ++stats_.closes;
  }

  bool is_open() const noexcept { return open_; }
  const SourceStats& stats() const noexcept { return stats_; }
  std::size_t size() const noexcept { return data_.size(); }

 private:
  std::string data_;
  std::size_t max_chunk_;
  std::size_t position_ = 0;
  std::size_t fail_offset_ = std::numeric_limits<std::size_t>::max();
  std::string fail_message_;
  bool open_ = false;
  SourceStats stats_;
};

}  // namespace gq
