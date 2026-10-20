-- 001_init.sql — schema base do serviço de pagamentos (compartilhado; NÃO altere).
-- Aplicada uma única vez por banco pelo migrador (internal/migrate), que registra
-- a versão em schema_migrations. Mudanças de schema vão em migrações novas.

-- Saldo de cada cliente, em centavos.
CREATE TABLE IF NOT EXISTS accounts (
    id            text   PRIMARY KEY,
    balance_cents bigint NOT NULL
);

-- Extrato: uma linha por cobrança efetivada. message_id é o MessageID do
-- PaymentRequested que originou a cobrança.
CREATE TABLE IF NOT EXISTS charges (
    id           bigserial   PRIMARY KEY,
    message_id   text        NOT NULL,
    account_id   text        NOT NULL REFERENCES accounts (id),
    amount_cents bigint      NOT NULL,
    created_at   timestamptz NOT NULL DEFAULT now()
);

-- Registro das mensagens já tratadas pelo consumidor (criada como "auditoria").
CREATE TABLE IF NOT EXISTS processed_messages (
    message_id   text        NOT NULL,
    processed_at timestamptz NOT NULL DEFAULT now()
);

-- Transactional outbox: eventos a publicar no broker.
--   event_id     = Event.ID (o downstream deduplica por ele)
--   event_type   = Event.Type
--   payload      = o Event inteiro em JSON (tags json de internal/events)
--   published_at = NULL enquanto o evento não foi aceito pelo broker
CREATE TABLE IF NOT EXISTS outbox (
    id           bigserial   PRIMARY KEY,
    event_id     text        NOT NULL,
    event_type   text        NOT NULL,
    payload      jsonb       NOT NULL,
    created_at   timestamptz NOT NULL DEFAULT now(),
    published_at timestamptz
);

CREATE INDEX IF NOT EXISTS outbox_pending_idx ON outbox (id) WHERE published_at IS NULL;
