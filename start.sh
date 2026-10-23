#!/usr/bin/env bash
# Prepara o ambiente de desafio-30. Idempotente: pode ser executado várias vezes.
#   GITQUEST_LOCAL=1 ./start.sh   usa g++ >= 14 e valgrind da sua máquina (Linux) em vez do Docker
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh

gq_title "GitQuest · desafio-30 — Dados que não cabiam no cache"

if [[ "${GITQUEST_LOCAL:-0}" == 1 ]]; then
  gq_require_cmd g++ "Instale g++ >= 14 ou rode sem GITQUEST_LOCAL para usar o Docker."
  gq_require_cmd make "Instale make (ex.: sudo apt install make)."
  gq_require_cmd valgrind "Instale valgrind (ex.: sudo apt install valgrind) ou rode sem GITQUEST_LOCAL."
  v=$(g++ -dumpfullversion 2>/dev/null || g++ -dumpversion)
  gq_version_ge "$v" 14 || gq_die "g++ $v é antigo demais; o desafio exige g++ >= 14 (ou use o Docker)."
  gq_ok "g++ $v e $(valgrind --version) locais"
  gq_warn "os limites foram calibrados na imagem gcc:14.2.0 + valgrind 3.19; localmente os números podem variar"
  make --no-print-directory build || gq_die "src/ não compilou — veja os erros acima"
  gq_info "Medindo as consultas do código atual (make profile)"
  make --no-print-directory profile || true
  gq_banner_next_steps "GITQUEST_LOCAL=1 ./test.sh" "5. Só a medição de cache:     GITQUEST_LOCAL=1 make profile"
  exit 0
fi

IMAGE=$(gq_toolchain_image desafio-30 Dockerfile)
gq_require_docker
gq_docker_build "$IMAGE" - Dockerfile
gq_info "Compilando o painel (C++20, gcc:14.2.0): ASan + UBSan para os testes e -O2 para a medição"
gq_docker_run "$IMAGE" 300 make --no-print-directory build ||
  gq_die "src/ não compilou — veja os erros acima"
gq_info "Medindo as consultas do código atual com o simulador de cache do Valgrind (make profile)"
GQ_DOCKER_EXTRA=(--security-opt "$(gq_seccomp_tsan)")
gq_docker_run "$IMAGE" 300 make --no-print-directory profile || true
gq_banner_next_steps "./test.sh" \
  "5. Arquivos da medição:       build/perf/callgrind.out.<n> (abra com callgrind_annotate)"
