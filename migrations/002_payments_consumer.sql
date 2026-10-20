-- 002_payments_consumer.sql — migração do time de pagamentos (consumidor).
-- Roda depois de 001_init.sql, uma vez por banco.

-- A checagem "já processei esta mensagem?" do consumidor ficou lenta quando a
-- tabela cresceu; este índice resolveu o problema de desempenho.
CREATE INDEX IF NOT EXISTS processed_messages_message_id_idx
    ON processed_messages (message_id);
