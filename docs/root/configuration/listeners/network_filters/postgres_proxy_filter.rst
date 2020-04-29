.. _config_network_filters_postgres_proxy:

Postgres proxy
================

The Postgres proxy filter decodes the wire protocol between a Postgres client (downstream) and a Postgres server
(upstream). The decoded information is currently used only to produce Postgres level statistics like sessions,
statements or transactions executed, among others. This current version does not decode SQL queries. Future versions may
add more statistics and more advanced capabilities. When the Postgres filter detects that a session is encrypted, the messages are ignored and no decoding takes
place. More information:

* Postgres :ref:`architecture overview <arch_overview_postgres>`

.. attention::

   The `postgres_proxy` filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.


.. warning::

   The `postgreql_proxy` filter was tested only with
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
  sessions_encrypted, Counter, Number of times the filter detected encrypted sessions
  sessions_unencrypted, Counter, Number of messages indicating unencrypted successful login
  statements, Counter, Total number of SQL statements
  statements_delete, Counter, Number of DELETE statements
  statements_insert, Counter, Number of INSERT statements
  statements_select, Counter, Number of SELECT statements
  statements_update, Counter, Number of UPDATE statements
  statements_other, Counter, "Number of statements other than DELETE, INSERT, SELECT or UPDATE"
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


