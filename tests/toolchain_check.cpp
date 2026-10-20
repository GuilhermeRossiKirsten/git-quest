// Verifica se o compilador/biblioteca padrão têm os recursos de C++23 usados
// no desafio. Só compila (-fsyntax-only); cada #error explica o que falta.
#include <version>

#if __cplusplus < 202302L
#error "compile com -std=c++23 (C++23 completo exigido pelo desafio)"
#endif

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202211L
#error "std::expected com operações monádicas (and_then/transform/or_else) indisponível: use GCC >= 13, Clang >= 17 (libc++) ou MSVC 19.36+"
#endif

#if !defined(__cpp_lib_print) || __cpp_lib_print < 202207L
#error "<print> (std::print/std::println) indisponível: use GCC >= 14, Clang >= 18 (libc++) ou MSVC 19.37+"
#endif

#if !defined(__cpp_lib_format) || __cpp_lib_format < 202106L
#error "std::format com verificação em tempo de compilação indisponível: use GCC >= 13"
#endif

#if !defined(__cpp_lib_ranges_to_container) || __cpp_lib_ranges_to_container < 202202L
#error "std::ranges::to indisponível: use GCC >= 14, Clang >= 17 (libc++) ou MSVC 19.34+"
#endif

#if !defined(__cpp_lib_ranges_enumerate) || __cpp_lib_ranges_enumerate < 202302L
#error "std::views::enumerate indisponível: use GCC >= 13, Clang >= 19 (libc++) ou MSVC 19.37+"
#endif

#if !defined(__cpp_lib_ranges_chunk_by) || __cpp_lib_ranges_chunk_by < 202202L
#error "std::views::chunk_by indisponível: use GCC >= 13, Clang >= 18 (libc++) ou MSVC 19.33+"
#endif

#if !defined(__cpp_lib_is_scoped_enum) || __cpp_lib_is_scoped_enum < 202011L
#error "std::is_scoped_enum indisponível: use GCC >= 11"
#endif

int main() {}
