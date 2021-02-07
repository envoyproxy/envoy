.. _install_sandboxes_mysql:

MySQL 过滤器
============

在这个例子中，我们展示了 :ref:`MySQL 过滤器 <config_network_filters_mysql_proxy>` 如何与 Envoy 代理一起使用。Envoy 的代理配置包含一个 MySQL 过滤器，它可以解析查询并收集 MySQL 特定的指标。

运行沙盒
~~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3：构建沙盒
****************

终端 1

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


步骤 4：使用 mysql 发出命令
***************************

使用 ``mysql`` 发出一些命令，并验证它们是否通过 Envoy 路由。注意，协议过滤器的当前实现已通过 MySQL v5.5 的测试。但是，由于协议实现方面的差异，它可能不适用于其他版本的 MySQL。

终端 1

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

步骤 5：检查 egress 统计信息
****************************

检查 egress 统计信息已更新。

终端 1

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

步骤 6：检查 TCP 统计信息
*************************

检查 TCP 统计信息已更新。

终端 1

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
