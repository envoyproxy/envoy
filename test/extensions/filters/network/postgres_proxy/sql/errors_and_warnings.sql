/* ERROR inside a explicit transaction */

BEGIN;                       -- (+) statements, statements_other and transactions
DROP TABLE IF EXISTS test;   -- (+) statements and statements_other
CREATE TABLE test();         -- (+) statements and statements_other
SELECT * FROM test;          -- (+) statements and statements_select
DELETE FROM test WHERE x=1;  -- (+) errors
COMMIT;                      -- (+) statements, statements_other and transactions_rollback

/*
postgres.egress_postgres.errors: 1
postgres.egress_postgres.frontend_commands: 6
postgres.egress_postgres.sessions: 1
postgres.egress_postgres.statements: 5
postgres.egress_postgres.statements_delete: 0
postgres.egress_postgres.statements_insert: 0
postgres.egress_postgres.statements_other: 4
postgres.egress_postgres.statements_select: 1
postgres.egress_postgres.statements_update: 0
postgres.egress_postgres.transactions: 1
postgres.egress_postgres.transactions_commit: 0
postgres.egress_postgres.transactions_rollback: 1
postgres.egress_postgres.unrecognized: 0
postgres.egress_postgres.warnings: 0
*/

/* WARNING:  there is already a transaction in progress */
BEGIN;     -- (+) statements, statements_other and transactions
BEGIN;     -- (+) statements and warnings
SELECT 1;  -- (+) statements and statements_select
COMMIT;    -- (+) statements, statements_other and transactions_commit

/*
postgres.egress_postgres.errors: 0
postgres.egress_postgres.frontend_commands: 4
postgres.egress_postgres.sessions: 1
postgres.egress_postgres.statements: 4
postgres.egress_postgres.statements_delete: 0
postgres.egress_postgres.statements_insert: 0
postgres.egress_postgres.statements_other: 3
postgres.egress_postgres.statements_select: 1
postgres.egress_postgres.statements_update: 0
postgres.egress_postgres.transactions: 1
postgres.egress_postgres.transactions_commit: 1
postgres.egress_postgres.transactions_rollback: 0
postgres.egress_postgres.unrecognized: 0
postgres.egress_postgres.warnings: 1
*/


/* ROLLBACK from an explicit transaction */
BEGIN;       -- (+) statements, statements_other and transactions
SELECT 1+1;  -- (+) statements and statements_select
ROLLBACK;    -- (+) statements, statements_other and transactions_rollback

/*
postgres.egress_postgres.errors: 0
postgres.egress_postgres.frontend_commands: 3
postgres.egress_postgres.sessions: 1
postgres.egress_postgres.statements: 3
postgres.egress_postgres.statements_delete: 0
postgres.egress_postgres.statements_insert: 0
postgres.egress_postgres.statements_other: 2
postgres.egress_postgres.statements_select: 1
postgres.egress_postgres.statements_update: 0
postgres.egress_postgres.transactions: 1
postgres.egress_postgres.transactions_commit: 0
postgres.egress_postgres.transactions_rollback: 1
postgres.egress_postgres.unrecognized: 0
postgres.egress_postgres.warnings: 0
*/
