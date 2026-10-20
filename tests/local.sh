# shellcheck shell=bash
# Verificações do modo local (GITQUEST_LOCAL=1), usadas por start.sh e test.sh.
# Requer scripts/lib/common.sh já carregado.

GO_WANT=1.25.3

gq_check_local_go() {
  gq_require_cmd go "Instale Go $GO_WANT (https://go.dev/dl/) ou rode sem GITQUEST_LOCAL=1 para usar o Docker."
  local have
  have=$(go env GOVERSION 2>/dev/null)
  have=${have#go}
  gq_version_ge "$have" "$GO_WANT" || gq_die "Go $have encontrado; este desafio exige Go >= $GO_WANT."
  # O detector de corridas (-race) depende de cgo e de um compilador C.
  if [[ "$(CGO_ENABLED=1 go env CGO_ENABLED)" != 1 ]] || ! command -v "$(go env CC)" >/dev/null 2>&1; then
    gq_die "go test -race precisa de cgo e de um compilador C ($(go env CC)) no PATH. Instale gcc ou use o modo Docker (padrão)."
  fi
  export CGO_ENABLED=1 GOTOOLCHAIN=local
  gq_ok "Go $have local com cgo ($(go env CC))"
}
