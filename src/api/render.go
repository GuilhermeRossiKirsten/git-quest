package api

import (
	"encoding/json"
	"net/http"
)

// writeJSON responde com status e v serializado em JSON.
func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

// writeError responde a qualquer falha. Simples e "funciona".
func writeError(w http.ResponseWriter, err error) {
	http.Error(w, err.Error(), http.StatusInternalServerError)
}

// decode lê o corpo JSON da requisição em v.
func decode(r *http.Request, v any) error {
	return json.NewDecoder(r.Body).Decode(v)
}
