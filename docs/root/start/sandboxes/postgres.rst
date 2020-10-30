.. _install_sandboxes_postgres:

Postgres Filter
===============

In this example, we show how the :ref:`Postgres filter <config_network_filters_postgres_proxy>`
can be used with the Envoy proxy. The Envoy proxy configuration includes a Postgres filter that
parses queries and collects Postgres-specific metrics.

.. include:: _include/docker-env-setup.rst


Step 3: Build the sandbox
*************************

.. code-block:: console

  $ pwd
  envoy/examples/postgres
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

         Name                      Command             State                             Ports
  ----------------------------------------------------------------------------------------------------------------------
  postgres_postgres_1   docker-entrypoint.sh postgres  Up      5432/tcp
  postgres_proxy_1      /docker-entrypoint.sh /usr ... Up      10000/tcp, 0.0.0.0:1999->1999/tcp, 0.0.0.0:8001->8001/tcp

Step 4: Issue commands using psql
*********************************

This example uses ``psql`` client inside a container to issue some commands and
verify they are routed via Envoy. Note that we should set the environment variable
``PGSSLMODE=disable`` to disable ``SSL`` because the current implementation of the
filter can't decode encrypted sessions.

.. code-block:: console

  $ docker run --rm -it --network envoymesh -e PGSSLMODE=disable postgres:latest psql -U postgres -h proxy -p 1999
  ... snip ...

  postgres=# CREATE DATABASE testdb;
  CREATE DATABASE
  postgres=# \c testdb
  You are now connected to database "testdb" as user "postgres".
  testdb=# CREATE TABLE tbl ( f SERIAL PRIMARY KEY );
  CREATE TABLE
  testdb=# INSERT INTO tbl VALUES (DEFAULT);
  INSERT 0 1
  testdb=# SELECT * FROM tbl;
   f
  ---
   1
  (1 row)

  testdb=# UPDATE tbl SET f = 2 WHERE f = 1;
  UPDATE 1
  testdb=# INSERT INTO tbl VALUES (DEFAULT);
  ERROR:  duplicate key value violates unique constraint "tbl_pkey"
  DETAIL:  Key (f)=(2) already exists.
  testdb=# DELETE FROM tbl;
  DELETE 1
  testdb=# INSERT INTO tbl VALUES (DEFAULT);
  INSERT 0 1
  testdb=# \q


Step 5: Check egress stats
**************************

Check egress stats were updated.

.. code-block:: console

  $ curl -s http://localhost:8001/stats?filter=egress_postgres
  postgres.egress_postgres.errors: 1
  postgres.egress_postgres.errors_error: 1
  postgres.egress_postgres.errors_fatal: 0
  postgres.egress_postgres.errors_panic: 0
  postgres.egress_postgres.errors_unknown: 0
  postgres.egress_postgres.messages: 42
  postgres.egress_postgres.messages_backend: 32
  postgres.egress_postgres.messages_frontend: 10
  postgres.egress_postgres.messages_unknown: 0
  postgres.egress_postgres.notices: 0
  postgres.egress_postgres.notices_debug: 0
  postgres.egress_postgres.notices_info: 0
  postgres.egress_postgres.notices_log: 0
  postgres.egress_postgres.notices_notice: 0
  postgres.egress_postgres.notices_unknown: 0
  postgres.egress_postgres.notices_warning: 0
  postgres.egress_postgres.sessions: 1
  postgres.egress_postgres.sessions_encrypted: 0
  postgres.egress_postgres.sessions_unencrypted: 1
  postgres.egress_postgres.statements: 7
  postgres.egress_postgres.statements_delete: 1
  postgres.egress_postgres.statements_insert: 2
  postgres.egress_postgres.statements_other: 2
  postgres.egress_postgres.statements_parse_error: 4
  postgres.egress_postgres.statements_parsed: 4
  postgres.egress_postgres.statements_select: 1
  postgres.egress_postgres.statements_update: 1
  postgres.egress_postgres.transactions: 7
  postgres.egress_postgres.transactions_commit: 7
  postgres.egress_postgres.transactions_rollback: 0


Step 6: Check TCP stats
***********************

Check TCP stats were updated.

.. code-block:: console

  $ curl -s http://localhost:8001/stats?filter=postgres_tcp
  tcp.postgres_tcp.downstream_cx_no_route: 0
  tcp.postgres_tcp.downstream_cx_rx_bytes_buffered: 0
  tcp.postgres_tcp.downstream_cx_rx_bytes_total: 373
  tcp.postgres_tcp.downstream_cx_total: 1
  tcp.postgres_tcp.downstream_cx_tx_bytes_buffered: 0
  tcp.postgres_tcp.downstream_cx_tx_bytes_total: 728
  tcp.postgres_tcp.downstream_flow_control_paused_reading_total: 0
  tcp.postgres_tcp.downstream_flow_control_resumed_reading_total: 0
  tcp.postgres_tcp.idle_timeout: 0
  tcp.postgres_tcp.max_downstream_connection_duration: 0
  tcp.postgres_tcp.upstream_flush_active: 0
  tcp.postgres_tcp.upstream_flush_total: 0
