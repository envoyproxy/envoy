/* <EXPLICT_TRANSACTIONS> */

-- (+) statements, statements_other and transactions
BEGIN;

-- (+) statements and statements_other
DROP TABLE IF EXISTS test;

-- (+) statements and statements_other
CREATE TABLE test();

-- (+) statements and statements_other
ALTER TABLE test
  ADD COLUMN f1 BIGSERIAL;

-- (+) statements and statements_other
ALTER TABLE test
  ADD COLUMN f2 VARCHAR(100),
  ADD COLUMN f3 TIMESTAMP;

-- (+) statements and statements_insert
INSERT INTO test (f2, f3)
  VALUES ('Name 1', now());

-- (+) statements and statements_insert
INSERT INTO test (f2, f3)
  VALUES ('Name 2', now()), ('Name 3', now());

-- (+) statements and statements_update
UPDATE test SET f2 = 'XXXX' WHERE f1 = 1;
 
-- (+) statements and statements_update
UPDATE test SET f2 = 'XXXX';

-- (+) statements and statements_select
SELECT * FROM test WHERE f1 = 1;

-- (+) statements and statements_select
SELECT * FROM test;

-- (+) statements and statements_delete
DELETE FROM test WHERE f1 = 1;

-- (+) statements and statements_delete
DELETE FROM test;

-- (+) statements, statements_other and transactions_commit
COMMIT;

/*
postgres.egress_postgres.errors: 0
postgres.egress_postgres.frontend_commands: 14
postgres.egress_postgres.sessions: 1
postgres.egress_postgres.statements: 14
postgres.egress_postgres.statements_delete: 2
postgres.egress_postgres.statements_insert: 2
postgres.egress_postgres.statements_other: 6
postgres.egress_postgres.statements_select: 2
postgres.egress_postgres.statements_update: 2
postgres.egress_postgres.transactions: 1
postgres.egress_postgres.transactions_commit: 1
postgres.egress_postgres.transactions_rollback: 0
postgres.egress_postgres.unrecognized: 0
postgres.egress_postgres.warnings: 0
*/

/* </EXPLICT_TRANSACTIONS> */
