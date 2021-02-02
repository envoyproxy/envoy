.. _config_network_filters_postgres_proxy:

Postgres 代理
================

Postgres 代理过滤器解码 Postgres 客户端（下游）和 Postgres 服务端（上游）之间的连接协议。解码后的信息会被用于生成 Postgres 级别的统计信息，例如其中的会话、语句和已执行的事务等。Postgres 代理过滤器还会解析 ``Query`` 和 ``Parse`` 消息中携带的 SQL 查询。
当成功解析 SQL 查询后, 元数据:ref:`metadata <config_network_filters_postgres_proxy_dynamic_metadata>` 会被创建, 这个元数据可能会被其他过滤器使用，比如 :ref:`RBAC <config_network_filters_rbac>` 。
当 Postgres 过滤器检测到会话已加密的时候，消息会被忽略并且不会进行解码。更多信息： 

* Postgres :ref:`架构概述 <arch_overview_postgres>`

.. attention::

   `postgres_proxy` 过滤器是实验性的，目前正在积极开发中。
   功能会随着时间的推移而扩展，并且配置结构也可能会发生变化。


.. warning::

   `postgreql_proxy` 过滤器仅使用 `Postgres frontend/backend protocol version 3.0`_ 测试过, 这个版本在 Postgres 7.4 被引入。因此不支持早期版本。无论如何，测试只限于非最终（EOL-ed）版本。

   .. _Postgres frontend/backend protocol version 3.0: https://www.postgresql.org/docs/current/protocol.html



配置
-------------

Postgres 代理过滤器应该与 TCP 代理串联在一起使用，如以下配置示例所示：

.. code-block:: yaml

    filter_chains:
    - filters:
      - name: envoy.filters.network.postgres_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy
          stat_prefix: postgres
      - name: envoy.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp
          cluster: postgres_cluster


.. _config_network_filters_postgres_proxy_stats:

统计
----------

每个配置的 Postgres 代理过滤器都有如下基于 postgres.<stat_prefix> 的统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 2, 1, 2

  errors, Counter, 服务器回复 ERROR message 的次数
  errors_error, Counter, 服务器回复 ERROR message 和 ERROR severity 的次数
  errors_fatal, Counter, 服务器回复 ERROR message 和 FATAL severity 的次数
  errors_panic, Counter, 服务器回复 ERROR message 和 PANIC severity 的次数
  errors_unknown, Counter, 服务器回复 ERROR message 但解码器无法解析它的次数
  messages, Counter, 过滤器处理的消息总数
  messages_backend, Counter, 过滤器检测到的后端消息总数
  messages_frontend, Counter, 过滤器检测到的前端消息数
  messages_unknown, Counter, 过滤器成功解码但不知道如何处理消息的次数
  sessions, Counter, 成功登陆总数
  sessions_encrypted, Counter, 过滤器检测到加密会话的次数
  sessions_unencrypted, Counter, 表示未加密成功登录的消息数
  statements, Counter, SQL语句总数
  statements_delete, Counter, DELETE 语句数
  statements_insert, Counter, INSERT 语句数
  statements_select, Counter, SELECT 语句数
  statements_update, Counter, UPDATE 语句数
  statements_other, Counter, “除 DELETE，INSERT，SELECT 或 UPDATE 以外的语句数”
  statements_parsed, Counter, 成功解析的 SQL 查询数
  statements_parse_error, Counter, 未成功解析的 SQL 查询数
  transactions, Counter, SQL事务总数
  transactions_commit, Counter, COMMIT 事务数
  transactions_rollback, Counter, ROLLBACK 事务数
  notices, Counter, NOTICE 消息总数
  notices_notice, Counter, 带有 NOTICE subtype 的 NOTICE 消息数
  notices_log, Counter, 带有 LOG subtype 的 NOTICE 消息数
  notices_warning, Counter, 带有 WARNING severity 的 NOTICE 消息数
  notices_debug, Counter, 带有 DEBUG severity 的 NOTICE 消息数
  notices_info, Counter, 带有 INFO severity 的 NOTICE 消息数
  notices_unknown, Counter, 无法识别的 NOTICE 消息数


.. _config_network_filters_postgres_proxy_dynamic_metadata:

动态元数据
----------------

Postgres 过滤器根据 ``Query`` 和 ``Parse`` 消息中携带的 SQL 语句发出动态元数据。``statements_parsed`` 统计计数器追踪有多少次 SQL 语句被成功解析，并创建元数据。这个元数据会以以下的格式发出：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  <table.db>, string, *table.db* 格式的资源名称。
  [], list, 表示在资源上执行的操作的字符串列表。操作可以是 insert/update/select/drop/delete/create/alter/show 操作之一。

.. attention::

   当前使用的解析器无法成功解析所有的 SQL 语句，并且不能假定所有的 SQL 查询都会成功生成动态元数据。
   目前基于 SQL 查询创建动态元数据是在尽力而为的基础上。如果解析 SQL 查询失败，``statements_parse_error`` 计数值会增加，并且创建日志消息，动态元数据不会生成，但是 Postgres 消息会继续转发到上游 Postgres 服务器。

可以通过设置 :ref:`enable_sql_parsing<envoy_v3_api_field_extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy.enable_sql_parsing>` 为 false 来禁用 解析 SQL 语句和发出动态元数据。
