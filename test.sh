#!/usr/bin/env bash
# Avaliador de desafio-23: código de saída 0 somente se todos os critérios passarem.
# Sobe o PostgreSQL do compose se preciso, gera o banco-modelo se ele não
# existir e roda tests/run.sh dentro do container: uma cópia limpa do banco
# recebe sql/indexes.sql + ANALYZE, e cada consulta de sql/queries/ é conferida
# (resultado contra uma referência, plano com EXPLAIN ANALYZE e buffers).
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=scripts/lib/common.sh
source scripts/lib/common.sh
# shellcheck source=env/compose.sh
source env/compose.sh

gq23_require
gq23_up
gq23_seed

gq_title "desafio-23 · índices, planos e resultados das consultas (leva ~1 min)"
mkdir -p build
rc=0
pg_exec timeout 300 bash /challenge/tests/run.sh >build/test.log 2>&1 || rc=$?

pass=0 fail=0
echo
while IFS= read -r line; do
  case "$line" in
    R\|1\|*) printf '  %s✔%s %s\n' "$GQ_GREEN" "$GQ_RESET" "${line#R|1|}"; pass=$((pass + 1)) ;;
    R\|0\|*) printf '  %s✘%s %s\n' "$GQ_RED" "$GQ_RESET" "${line#R|0|}"; fail=$((fail + 1)) ;;
    D\|*) printf '        %s\n' "${line#D|}" ;;
    "") ;;
    *) printf '        %s\n' "$line" ;;
  esac
done <build/test.log

echo
if ((rc != 0)); then
  gq_error "o avaliador terminou com código $rc (timeout de 300 s?) — veja build/test.log"
  exit 1
fi
if ((pass == 0)); then
  gq_error "nenhum critério foi avaliado — veja build/test.log"
  exit 1
fi
if ((fail == 0)); then
  gq_ok "$pass critério(s) satisfeito(s). Desafio concluído!"
  exit 0
fi
gq_error "$fail critério(s) pendente(s), $pass satisfeito(s). Log completo: build/test.log"
exit 1
