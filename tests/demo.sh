#!/usr/bin/env bash
# Demonstração do minisup: sobe examples/demo.conf, mostra os processos e os
# descritores dos serviços, pede o desligamento com SIGTERM e mostra o que
# sobrou. Chamado por `make demo` (dentro do container ou com GITQUEST_LOCAL=1).
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

out=build/demo.out
log=build/demo.log
rm -f "$out" "$log"

show_procs() {
  # pgrep não mostra zumbis com PPID/estado lado a lado; aqui o ps é a ferramenta certa.
  # shellcheck disable=SC2009
  ps -eo pid,ppid,pgid,stat,args | grep -E '^ *PID|minisup|gq-helper|gq-neto' | grep -v grep | sed 's/^/    /'
}

echo "==> build/minisup -g 1000 -l $log examples/demo.conf"
build/minisup -g 1000 -l "$log" examples/demo.conf >"$out" 2>&1 &
sup=$!
sleep 2

echo "==> Processos 2 s depois (minisup = PID $sup; STAT Z = zumbi)"
show_procs

echo "==> Descritores abertos em cada filho direto do minisup"
for child in $(pgrep -P "$sup"); do
  printf '    PID %s (%s):' "$child" "$(tr '\0' ' ' <"/proc/$child/cmdline" 2>/dev/null | cut -c1-40)"
  for fd in /proc/"$child"/fd/*; do
    printf ' %s->%s' "${fd##*/}" "$(readlink "$fd" 2>/dev/null)"
  done
  echo
done

echo "==> kill -TERM $sup (carência de 1 s)"
kill -TERM "$sup"
for _ in $(seq 50); do
  kill -0 "$sup" 2>/dev/null || break
  sleep 0.1
done
if kill -0 "$sup" 2>/dev/null; then
  echo "    o minisup AINDA está vivo 5 s depois do SIGTERM; enviando SIGKILL"
  kill -KILL "$sup"
  wait "$sup" 2>/dev/null
  echo "    (código de saída irrelevante: foi morto à força)"
else
  rc=0
  wait "$sup" || rc=$?
  echo "    o minisup saiu com código $rc"
fi
sleep 0.3

echo "==> Processos depois que o minisup saiu"
show_procs

echo "==> Saída do minisup (últimas 25 linhas de $out)"
tail -25 "$out" | sed 's/^/    /'
echo "==> Registro de eventos ($log)"
sed 's/^/    /' "$log" 2>/dev/null | tail -30
echo
echo "    Esperado: nenhum zumbi; nada além de 0, 1 e 2 nos descritores dos serviços;"
echo "    saída do minisup poucos instantes depois da carência; nenhum gq-helper/gq-neto"
echo "    sobrevivendo; resumo final com 'morto pelo sinal 9' para 'instavel' e 'teimoso'."
