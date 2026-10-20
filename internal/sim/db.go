package sim

import (
	"context"
	"fmt"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

// Connect abre um pool para o banco dbName no mesmo servidor de baseURL, com
// o Tracer instalado (quedas simuladas e latência).
func Connect(ctx context.Context, baseURL, dbName string, tracer *Tracer, maxConns int32) (*pgxpool.Pool, error) {
	cfg, err := pgxpool.ParseConfig(baseURL)
	if err != nil {
		return nil, fmt.Errorf("DATABASE_URL inválida: %w", err)
	}
	if dbName != "" {
		cfg.ConnConfig.Database = dbName
	}
	if tracer != nil {
		cfg.ConnConfig.Tracer = tracer
	}
	if maxConns > 0 {
		cfg.MaxConns = maxConns
	}
	db, err := pgxpool.NewWithConfig(ctx, cfg)
	if err != nil {
		return nil, err
	}
	if err := db.Ping(ctx); err != nil {
		db.Close()
		return nil, fmt.Errorf("PostgreSQL inacessível (%s): %w", cfg.ConnConfig.Host, err)
	}
	return db, nil
}

// RecreateDatabase apaga (se existir) e cria o banco name, vazio.
func RecreateDatabase(ctx context.Context, admin *pgxpool.Pool, name string) error {
	ident := pgx.Identifier{name}.Sanitize()
	if _, err := admin.Exec(ctx, "DROP DATABASE IF EXISTS "+ident+" WITH (FORCE)"); err != nil {
		return fmt.Errorf("apagando banco %s: %w", name, err)
	}
	if _, err := admin.Exec(ctx, "CREATE DATABASE "+ident); err != nil {
		return fmt.Errorf("criando banco %s: %w", name, err)
	}
	return nil
}

// DropDatabase apaga o banco name, derrubando conexões abertas.
func DropDatabase(ctx context.Context, admin *pgxpool.Pool, name string) error {
	_, err := admin.Exec(ctx, "DROP DATABASE IF EXISTS "+pgx.Identifier{name}.Sanitize()+" WITH (FORCE)")
	return err
}

// SeedAccounts cria as contas com seus saldos iniciais.
func SeedAccounts(ctx context.Context, db *pgxpool.Pool, accounts []Account) error {
	for _, a := range accounts {
		if _, err := db.Exec(ctx, `INSERT INTO accounts (id, balance_cents) VALUES ($1, $2)`,
			a.ID, a.InitialCents); err != nil {
			return fmt.Errorf("criando conta %s: %w", a.ID, err)
		}
	}
	return nil
}
