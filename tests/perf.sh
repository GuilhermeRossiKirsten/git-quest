#!/usr/bin/env bash
# Mede cada consulta do painel com o simulador de cache do Valgrind e compara
# com os limites de tests/budgets.txt. Determinístico: nada de tempo de relógio.
#
#   bash tests/perf.sh           chamado por `make test` (tests/run.sh) e `make profile`
#
# Escreve "aprovados reprovados" em build/perf/result para o tests/run.sh.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

PERF=build/perf
LOGS=build/logs
BUDGETS=tests/budgets.txt
mkdir -p "$PERF" "$LOGS"
rm -f "$PERF"/callgrind.out* "$PERF/result"

# Cache simulada FIXA (não a da sua máquina): é isso que torna os números reprodutíveis.
CACHE=("--I1=32768,8,64" "--D1=32768,8,64" "--LL=2097152,16,64")

ok()  { printf '  \342\234\224 %s\n' "$1"; }
bad() { printf '  \342\234\230 %s\n      motivo: %s\n' "$1" "$2"; }

pass=0
fail=0
n_criteria=$(grep -cvE '^[[:space:]]*(#|$)|[[:space:]]-[[:space:]]' "$BUDGETS")
finish() {
  echo "$pass $fail" >"$PERF/result"
  ((fail == 0))
}
fail_all() {  # fail_all <motivo>
  bad "medição de cache das consultas" "$1"
  fail=$((fail + n_criteria))
  finish
  exit 1
}

# 1 000 000 -> "1 000 000"
fmt() { awk -v n="$1" 'BEGIN { s = sprintf("%d", n); o = ""; while (length(s) > 3) { o = " " substr(s, length(s) - 2) o; s = substr(s, 1, length(s) - 3) } print s o }'; }

echo "==> Medição de cache (valgrind --tool=callgrind --cache-sim=yes, D1 32 KiB/8 vias, LL 2 MiB/16 vias)"

command -v valgrind >/dev/null 2>&1 ||
  fail_all "valgrind não encontrado (use ./test.sh com Docker; no modo local instale valgrind 3.19)"
vv=$(valgrind --version 2>/dev/null)
[[ "$vv" == valgrind-3.19.0 ]] ||
  echo "  aviso: $vv encontrado; os limites foram calibrados com valgrind-3.19.0 (imagem do desafio)"

if ! make --no-print-directory build/opt/bin/bench >"$LOGS/bench.compile.log" 2>&1; then
  grep -E 'error:' "$LOGS/bench.compile.log" | head -5 | cut -c1-200 | sed 's/^/        /'
  fail_all "o bench (-O2) não compilou (log: $LOGS/bench.compile.log)"
fi

# Sem ASLR, o heap e os mmap caem sempre nos mesmos endereços (e nos mesmos
# conjuntos da cache simulada): execuções repetidas dão números idênticos.
NORAND=()
if setarch "$(uname -m)" -R true 2>/dev/null; then NORAND=(setarch "$(uname -m)" -R); fi

rc=0
timeout --kill-after=10 240 "${NORAND[@]}" valgrind --tool=callgrind --cache-sim=yes "${CACHE[@]}" \
  --collect-atstart=no --callgrind-out-file="$PERF/callgrind.out" \
  build/opt/bin/bench >"$LOGS/bench.log" 2>"$LOGS/valgrind.log" || rc=$?
if ((rc == 124 || rc == 137)); then
  fail_all "tempo esgotado rodando o bench sob o Valgrind (240 s) — laço infinito?"
fi
if ((rc != 0)) || ! grep -qx OK "$LOGS/bench.log"; then
  grep -E '^(DIVERGENCIA|FALHA)' "$LOGS/bench.log" | head -6 | sed 's/^/        /'
  fail_all "o bench terminou com código $rc ou deu respostas diferentes do gabarito (log: $LOGS/bench.log)"
fi

# region -> "Ir D1 LL", a partir dos arquivos gerados por CALLGRIND_DUMP_STATS_AT.
declare -A IR D1 LL
for f in "$PERF"/callgrind.out.*; do
  [[ -e "$f" ]] || continue
  if ! grep -q '^desc: D1 cache: *32768 B, 64 B, 8-way' "$f" || ! grep -q '^desc: LL cache: *2097152 B, 64 B, 16-way' "$f"; then
    fail_all "o Valgrind não usou a cache simulada pedida (veja $f)"
  fi
  region=$(sed -n 's/^desc: Trigger: Client Request: //p' "$f")
  read -r ir d1 ll < <(awk '
    /^events:/ { for (i = 2; i <= NF; i++) col[$i] = i - 1 }
    /^totals:/ { print $(col["Ir"] + 1), $(col["D1mr"] + 1) + $(col["D1mw"] + 1), $(col["DLmr"] + 1) + $(col["DLmw"] + 1) }' "$f")
  [[ -n "$region" && -n "${ir:-}" ]] || continue
  IR[$region]=$ir
  D1[$region]=$d1
  LL[$region]=$ll
done

declare -A WHAT=([Ir]="instruções executadas (Ir)" [D1]="misses de D1" [LL]="misses de LL")
while read -r region metric limit initial reference; do
  [[ -z "$region" || "$region" == \#* ]] && continue
  case $metric in
    Ir) value=${IR[$region]:-} ;;
    D1) value=${D1[$region]:-} ;;
    LL) value=${LL[$region]:-} ;;
    *) value= ;;
  esac
  [[ "$limit" == - ]] && continue
  label="$region: ${WHAT[$metric]} <= $(fmt "$limit")"
  if [[ -z "$value" ]]; then
    bad "$label" "a região '$region' não foi medida (o bench terminou antes?)"
    fail=$((fail + 1))
  elif ((value <= limit)); then
    ok "$label  (medido $(fmt "$value"); código inicial $(fmt "$initial"))"
    pass=$((pass + 1))
  else
    pct=$(awk -v v="$value" -v i="$initial" 'BEGIN { printf "%.0f", 100 * v / i }')
    bad "$label" "medido $(fmt "$value") = ${pct}% do código inicial ($(fmt "$initial")); referência ~$(fmt "$reference")"
    fail=$((fail + 1))
  fi
done <"$BUDGETS"

echo
printf '      %-15s %14s %12s %12s\n' "região" "Ir" "misses D1" "misses LL"
for region in ingest revenue units_by_day refunded_cents basket_value; do
  printf '      %-15s %14s %12s %12s\n' "$region" "$(fmt "${IR[$region]:-0}")" "$(fmt "${D1[$region]:-0}")" \
    "$(fmt "${LL[$region]:-0}")"
done
echo "      (arquivos para callgrind_annotate: $PERF/callgrind.out.<n>)"
finish
