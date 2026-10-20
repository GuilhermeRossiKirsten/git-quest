// Package migrate aplica os arquivos migrations/NNN_*.sql em ordem, uma única
// vez por banco. Cada arquivo roda numa transação junto com o registro da sua
// versão em schema_migrations — por isso aplicar de novo é inofensivo.
package migrate

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

// Apply aplica as migrações pendentes do diretório dir e devolve as versões
// aplicadas nesta chamada. Uma migração já aplicada cujo conteúdo mudou é erro:
// o banco precisa ser recriado.
func Apply(ctx context.Context, db *pgxpool.Pool, dir string) ([]string, error) {
	files, err := filepath.Glob(filepath.Join(dir, "*.sql"))
	if err != nil {
		return nil, err
	}
	if len(files) == 0 {
		return nil, fmt.Errorf("nenhuma migração encontrada em %s", dir)
	}
	sort.Strings(files)

	if _, err := db.Exec(ctx, `CREATE TABLE IF NOT EXISTS schema_migrations (
		version    text        PRIMARY KEY,
		checksum   text        NOT NULL,
		applied_at timestamptz NOT NULL DEFAULT now())`); err != nil {
		return nil, fmt.Errorf("criando schema_migrations: %w", err)
	}

	var applied []string
	for _, f := range files {
		body, err := os.ReadFile(f)
		if err != nil {
			return applied, err
		}
		version := strings.TrimSuffix(filepath.Base(f), ".sql")
		sum := sha256.Sum256(body)
		checksum := hex.EncodeToString(sum[:])

		did, err := applyOne(ctx, db, version, checksum, string(body))
		if err != nil {
			return applied, fmt.Errorf("migração %s: %w", version, err)
		}
		if did {
			applied = append(applied, version)
		}
	}
	return applied, nil
}

func applyOne(ctx context.Context, db *pgxpool.Pool, version, checksum, sql string) (bool, error) {
	tx, err := db.Begin(ctx)
	if err != nil {
		return false, err
	}
	defer tx.Rollback(ctx) //nolint:errcheck // sem efeito depois do Commit

	// Serializa migradores concorrentes (ex.: start.sh e test.sh ao mesmo tempo).
	if _, err := tx.Exec(ctx, `SELECT pg_advisory_xact_lock(2020)`); err != nil {
		return false, err
	}
	var old string
	err = tx.QueryRow(ctx, `SELECT checksum FROM schema_migrations WHERE version = $1`, version).Scan(&old)
	switch {
	case err == nil && old == checksum:
		return false, nil
	case err == nil:
		return false, fmt.Errorf("já foi aplicada com outro conteúdo; recrie o banco (./start.sh --reset)")
	case !errors.Is(err, pgx.ErrNoRows):
		return false, err
	}
	// Sem argumentos o pgx usa o protocolo simples, que aceita vários comandos.
	if _, err := tx.Exec(ctx, sql); err != nil {
		return false, err
	}
	if _, err := tx.Exec(ctx, `INSERT INTO schema_migrations (version, checksum) VALUES ($1, $2)`,
		version, checksum); err != nil {
		return false, err
	}
	return true, tx.Commit(ctx)
}
