.. _config_network_filters_postgres_proxy:

Postgres proxy
==============

The Postgres proxy filter decodes the wire protocol between a Postgres client (downstream) and a Postgres server
(upstream). The decoded information is used to produce Postgres level statistics like sessions,
statements or transactions executed, among others. The Postgres proxy filter parses SQL queries carried in ``Query`` and ``Parse`` messages.
When SQL query has been parsed successfully, the :ref:`metadata <config_network_filters_postgres_proxy_dynamic_metadata>` is created,
which may be used by other filters like :ref:`RBAC <config_network_filters_rbac>`.
When the Postgres filter detects that a session is encrypted, the messages are ignored and no decoding takes
place. More information:

* Postgres :ref:`architecture overview <arch_overview_postgres>`

.. attention::

   The ``postgres_proxy`` filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.


.. warning::

   The ``postgreql_proxy`` filter was tested only with
   `Postgres frontend/backend protocol version 3.0`_, which was introduced in
   Postgres 7.4. Earlier versions are thus not supported. Testing is limited
   anyway to not EOL-ed versions.

   .. _Postgres frontend/backend protocol version 3.0: https://www.postgresql.org/docs/current/protocol.html



Configuration
-------------

The Postgres proxy filter should be chained with the TCP proxy as shown in the configuration
example below:

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

Statistics
----------

Every configured Postgres proxy filter has statistics rooted at postgres.<stat_prefix> with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 2, 1, 2

  errors, Counter, Number of times the server replied with ERROR message
  errors_error, Counter, Number of times the server replied with ERROR message with ERROR severity
  errors_fatal, Counter, Number of times the server replied with ERROR message with FATAL severity
  errors_panic, Counter, Number of times the server replied with ERROR message with PANIC severity
  errors_unknown, Counter, Number of times the server replied with ERROR message but the decoder could not parse it
  messages, Counter, Total number of messages processed by the filter
  messages_backend, Counter, Total number of backend messages detected by the filter
  messages_frontend, Counter, Number of frontend messages detected by the filter
  messages_unknown, Counter, Number of times the filter successfully decoded a message but did not know what to do with it
  sessions, Counter, Total number of successful logins
  sessions_encrypted, Counter, Number of times the filter detected and passed upstream encrypted sessions
  sessions_terminated_ssl, Counter, Number of times the filter terminated SSL sessions
  sessions_unencrypted, Counter, Number of messages indicating unencrypted successful login
  sessions_upstream_ssl_success, Counter, Number of times the filter established upstream SSL connection
  sessions_upstream_ssl_failed, Counter, Number of times the filter failed to establish upstream SSL connection
  statements, Counter, Total number of SQL statements
  statements_delete, Counter, Number of DELETE statements
  statements_insert, Counter, Number of INSERT statements
  statements_select, Counter, Number of SELECT statements
  statements_update, Counter, Number of UPDATE statements
  statements_other, Counter, "Number of statements other than DELETE, INSERT, SELECT or UPDATE"
  statements_parsed, Counter, Number of SQL queries parsed successfully
  statements_parse_error, Counter, Number of SQL queries not parsed successfully
  transactions, Counter, Total number of SQL transactions
  transactions_commit, Counter, Number of COMMIT transactions
  transactions_rollback, Counter, Number of ROLLBACK transactions
  notices, Counter, Total number of NOTICE messages
  notices_notice, Counter, Number of NOTICE messages with NOTICE subtype
  notices_log, Counter, Number of NOTICE messages with LOG subtype
  notices_warning, Counter, Number ofr NOTICE messags with WARNING severity
  notices_debug, Counter, Number of NOTICE messages with DEBUG severity
  notices_info, Counter, Number of NOTICE messages with INFO severity
  notices_unknown, Counter, Number of NOTICE messages which could not be recognized


.. _config_network_filters_postgres_proxy_dynamic_metadata:

Dynamic Metadata
----------------

The Postgres filter emits Dynamic Metadata based on SQL statements carried in ``Query`` and ``Parse`` messages. ``statements_parsed`` statistics Counter tracks how many times
SQL statement was parsed successfully and metadata was created. The metadata is emitted in the following format:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  <table.db>, string, The resource name in *table.db* format.
  [], list, A list of strings representing the operations executed on the resource. Operations can be one of insert/update/select/drop/delete/create/alter/show.

.. attention::

   Currently used parser does not successfully parse all SQL statements and it cannot be assumed that all SQL queries will successfully produce Dynamic Metadata.
   Creating Dynamic Metadata from SQL queries is on best-effort basis at the moment. If parsing of an SQL query fails, ``statements_parse_error`` counter is increased, log message is created, Dynamic Metadata is not
   produced, but the Postgres message is still forwarded to upstream Postgres server.

Parsing SQL statements and emitting Dynamic Metadata can be disabled by setting :ref:`enable_sql_parsing<envoy_v3_api_field_extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy.enable_sql_parsing>` to false.
