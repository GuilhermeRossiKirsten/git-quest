# Toolchain do desafio: apenas ferramentas, nenhum código do desafio é copiado.
# O código é montado em /work por gq_docker_run (sem rede, usuário não-root).
# A imagem oficial gcc:14.2.0 (Debian bookworm) já traz gcc 14.2, make, ASan/UBSan,
# python3 (avaliador) e procps (ps/pgrep, usados na demonstração).
FROM gcc:14.2.0
