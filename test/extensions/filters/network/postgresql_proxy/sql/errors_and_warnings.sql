/* ERROR inside a explicit transaction */

BEGIN;                       -- (+) statements, statements_other and transactions
DROP TABLE IF EXISTS test;   -- (+) statements and statements_other
CREATE TABLE test();         -- (+) statements and statements_other
SELECT * FROM test;          -- (+) statements and statements_select
DELETE FROM test WHERE x=1;  -- (+) errors
COMMIT;                      -- (+) statements, statements_other and transactions_rollback

/*
postgresql.egress_postgresql.errors: 1
postgresql.egress_postgresql.frontend_commands: 6
postgresql.egress_postgresql.sessions: 1
postgresql.egress_postgresql.statements: 5
postgresql.egress_postgresql.statements_delete: 0
postgresql.egress_postgresql.statements_insert: 0
postgresql.egress_postgresql.statements_other: 4
postgresql.egress_postgresql.statements_select: 1
postgresql.egress_postgresql.statements_update: 0
postgresql.egress_postgresql.transactions: 1
postgresql.egress_postgresql.transactions_commit: 0
postgresql.egress_postgresql.transactions_rollback: 1
postgresql.egress_postgresql.unrecognized: 0
postgresql.egress_postgresql.warnings: 0
*/

/* WARNING:  there is already a transaction in progress */
BEGIN;     -- (+) statements, statements_other and transactions
BEGIN;     -- (+) statements and warnings
SELECT 1;  -- (+) statements and statements_select
COMMIT;    -- (+) statements, statements_other and transactions_commit

/*
postgresql.egress_postgresql.errors: 0
postgresql.egress_postgresql.frontend_commands: 4
postgresql.egress_postgresql.sessions: 1
postgresql.egress_postgresql.statements: 4
postgresql.egress_postgresql.statements_delete: 0
postgresql.egress_postgresql.statements_insert: 0
postgresql.egress_postgresql.statements_other: 3
postgresql.egress_postgresql.statements_select: 1
postgresql.egress_postgresql.statements_update: 0
postgresql.egress_postgresql.transactions: 1
postgresql.egress_postgresql.transactions_commit: 1
postgresql.egress_postgresql.transactions_rollback: 0
postgresql.egress_postgresql.unrecognized: 0
postgresql.egress_postgresql.warnings: 1
*/


/* ROLLBACK from an explicit transaction */
BEGIN;       -- (+) statements, statements_other and transactions
SELECT 1+1;  -- (+) statements and statements_select
ROLLBACK;    -- (+) statements, statements_other and transactions_rollback

/*
postgresql.egress_postgresql.errors: 0
postgresql.egress_postgresql.frontend_commands: 3
postgresql.egress_postgresql.sessions: 1
postgresql.egress_postgresql.statements: 3
postgresql.egress_postgresql.statements_delete: 0
postgresql.egress_postgresql.statements_insert: 0
postgresql.egress_postgresql.statements_other: 2
postgresql.egress_postgresql.statements_select: 1
postgresql.egress_postgresql.statements_update: 0
postgresql.egress_postgresql.transactions: 1
postgresql.egress_postgresql.transactions_commit: 0
postgresql.egress_postgresql.transactions_rollback: 1
postgresql.egress_postgresql.unrecognized: 0
postgresql.egress_postgresql.warnings: 0
*/
