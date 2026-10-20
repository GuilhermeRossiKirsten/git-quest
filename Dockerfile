# Toolchain do desafio: apenas ferramentas, nenhum código do desafio é copiado.
# gcc:14.2.0 traz g++ 14.2 + libstdc++ com std::expected, <print>, ranges::to,
# views::enumerate e views::chunk_by; make e coreutils (timeout, diff) já vêm na imagem.
FROM gcc:14.2.0
