.. _install_sandboxes_mysql:

MySQL Filter
============

In this example, we show how the :ref:`MySQL filter <config_network_filters_mysql_proxy>` can be used with the Envoy proxy. The Envoy proxy configuration includes a MySQL filter that parses queries and collects MySQL-specific
metrics.


Running the Sandboxes
~~~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of the services.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`` and ``docker-compose``.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo**

If you have not cloned the Envoy repo, clone it with:

``git clone git@github.com:envoyproxy/envoy``

or

``git clone https://github.com/envoyproxy/envoy.git``

**Step 3: Build the sandbox**

Terminal 1

.. code-block:: console

  $ pwd
  envoy/examples/mysql
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

      Name                   Command               State                             Ports
  ------------------------------------------------------------------------------------------------------------------
  mysql_mysql_1   docker-entrypoint.sh mysqld      Up      0.0.0.0:3306->3306/tcp
  mysql_proxy_1   /docker-entrypoint.sh /bin       Up      10000/tcp, 0.0.0.0:1999->1999/tcp, 0.0.0.0:8001->8001/tcp


**Step 4: Issue commands using mysql**

Use ``mysql`` to issue some commands and verify they are routed via Envoy. Note
that the current implementation of the protocol filter was tested with MySQL
v5.5. It may, however, not work with other versions of MySQL due to differences
in the protocol implementation.

Terminal 1

.. code-block:: console

  $ docker run --rm -it --network envoymesh mysql:5.5 mysql -h envoy -P 1999 -u root
  ... snip ...

  mysql> CREATE DATABASE test;
  Query OK, 1 row affected (0.00 sec)

  mysql> USE test;
  Database changed
  mysql> CREATE TABLE test ( text VARCHAR(255) );
  Query OK, 0 rows affected (0.01 sec)

  mysql> SELECT COUNT(*) FROM test;
  +----------+
  | COUNT(*) |
  +----------+
  |        0 |
  +----------+
  1 row in set (0.01 sec)

  mysql> INSERT INTO test VALUES ('hello, world!');
  Query OK, 1 row affected (0.00 sec)

  mysql> SELECT COUNT(*) FROM test;
  +----------+
  | COUNT(*) |
  +----------+
  |        1 |
  +----------+
  1 row in set (0.00 sec)

  mysql> exit
  Bye

**Step 5: Check egress stats**

Check egress stats were updated.

Terminal 1

.. code-block:: console

  $ curl -s http://localhost:8001/stats?filter=egress_mysql
  mysql.egress_mysql.auth_switch_request: 0
  mysql.egress_mysql.decoder_errors: 0
  mysql.egress_mysql.login_attempts: 1
  mysql.egress_mysql.login_failures: 0
  mysql.egress_mysql.protocol_errors: 0
  mysql.egress_mysql.queries_parse_error: 0
  mysql.egress_mysql.queries_parsed: 7
  mysql.egress_mysql.sessions: 1
  mysql.egress_mysql.upgraded_to_ssl: 0

**Step 6: Check TCP stats**

Check TCP stats were updated.

Terminal 1

.. code-block:: console

  $ curl -s http://localhost:8001/stats?filter=mysql_tcp
  tcp.mysql_tcp.downstream_cx_no_route: 0
  tcp.mysql_tcp.downstream_cx_rx_bytes_buffered: 0
  tcp.mysql_tcp.downstream_cx_rx_bytes_total: 347
  tcp.mysql_tcp.downstream_cx_total: 1
  tcp.mysql_tcp.downstream_cx_tx_bytes_buffered: 0
  tcp.mysql_tcp.downstream_cx_tx_bytes_total: 702
  tcp.mysql_tcp.downstream_flow_control_paused_reading_total: 0
  tcp.mysql_tcp.downstream_flow_control_resumed_reading_total: 0
  tcp.mysql_tcp.idle_timeout: 0
  tcp.mysql_tcp.upstream_flush_active: 0
  tcp.mysql_tcp.upstream_flush_total: 0
