#pragma once
// FORNECIDO PELO DESAFIO — NÃO edite (o avaliador usa esta versão).
//
// Árvore de diretórios "virtual" usada pelo coletor de logs: o snapshot de um
// bucket de armazenamento, já carregado em memória. Pode ter centenas de
// milhares de arquivos e milhares de níveis de profundidade.
//
// Copiar um Dir copia a subárvore inteira; cada cópia é contada em
// gq::Dir::copies() para que o custo fique visível.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace gq {

struct File {
  std::string name;
  std::size_t size = 0;
};

class Dir {
 public:
  explicit Dir(std::string dir_name) : name(std::move(dir_name)) {}

  Dir(const Dir& other) : name(other.name), files(other.files), subdirs(other.subdirs) {
    ++copy_counter();
  }
  Dir& operator=(const Dir& other) {
    if (this != &other) {
      name = other.name;
      files = other.files;
      subdirs = other.subdirs;
      ++copy_counter();
    }
    return *this;
  }
  Dir(Dir&&) noexcept = default;
  Dir& operator=(Dir&&) noexcept = default;
  ~Dir() = default;

  // Adiciona um subdiretório e devolve uma referência a ele (válida até o
  // próximo add_dir neste mesmo diretório).
  Dir& add_dir(std::string dir_name) { return subdirs.emplace_back(std::move(dir_name)); }
  void add_file(std::string file_name, std::size_t file_size = 0) {
    files.push_back(File{std::move(file_name), file_size});
  }

  // Quantos Dir foram copiados desde o início do processo (ou do último reset).
  static std::size_t copies() noexcept { return copy_counter(); }
  static void reset_copies() noexcept { copy_counter() = 0; }

  std::string name;
  std::vector<File> files;
  std::vector<Dir> subdirs;

 private:
  static std::size_t& copy_counter() noexcept {
    static std::size_t count = 0;
    return count;
  }
};

}  // namespace gq
