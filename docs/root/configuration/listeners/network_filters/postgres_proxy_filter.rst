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

   The ``postgres_proxy`` filter was tested only with
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


.. _config_network_filters_postgres_proxy_rbac:

Connection authorization
------------------------

The ``rules`` field enables connection-level RBAC in the Postgres proxy filter. The filter still
requires trusted downstream and upstream peers. Use these rules only within that trust model.

The rules can match connection properties, such as addresses and ports, and the downstream TLS
peer identity when Envoy terminates TLS. The packet and encryption mode determine which other
fields the engine can see.

* For a plaintext or decrypted ``StartupMessage``, the filter evaluates the rules with the startup
  attributes before it forwards the packet.
* In SSL passthrough mode, the filter first evaluates the rules on the ``SSLRequest`` using the
  available connection properties and the packet type. With enforcing rules, an upstream ``S``
  switches the connection to encrypted passthrough, and an upstream ``N`` causes the filter to
  evaluate the next startup packet. To enforce user or database rules on encrypted sessions,
  configure Envoy to terminate downstream TLS.
* With enforcing rules, Envoy declines a ``GSSENCRequest`` locally. A client that accepts the
  refusal can continue with a plaintext startup or attempt SSL negotiation.
* A decoded ``CancelRequest`` carries no startup attributes. To permit cancellation alongside
  a user-based ``ALLOW`` policy, add a policy that matches the filter's ``packet_type`` metadata
  exactly to ``cancel_request``, optionally combined with connection restrictions. PostgreSQL
  verifies the cancel key. The filter accepts the legacy four-byte key and PostgreSQL 18's
  variable-length keys. A cancel inside an opaque TLS session cannot be identified by the filter.
  With ``upstream_ssl: REQUIRE``, the filter negotiates upstream TLS before forwarding a decoded
  cancellation. Legacy clients can still send a plaintext cancellation when downstream TLS is
  required.

For example, this policy entry permits cancellation in an ``ALLOW`` ruleset.

.. code-block:: yaml

   allow_cancel:
     principals: [{any: true}]
     permissions:
     - metadata:
         filter: envoy.filters.network.postgres_proxy.rbac
         path: [{key: packet_type}]
         value: {string_match: {exact: cancel_request}}

``ALLOW`` and ``DENY`` rules enforce authorization. An empty ``ALLOW`` ruleset denies all
connections. With enforcing rules, oversized or invalid client startup packets, including a
PostgreSQL 17 direct TLS handshake, are rejected. ``LOG`` evaluates recognized packets for logging
and does not cause the filter to reject an oversized packet or a packet that fails startup validation.

Use the TCP proxy's ``upstream_connect_mode: ON_DOWNSTREAM_DATA`` and set
``max_early_data_bytes`` (for example, ``16384``) to defer opening an upstream connection until
the Postgres filter forwards data. An allowed passthrough ``SSLRequest`` still requires an
upstream connection to get the server's reply.


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
  authorization_allowed, Counter, Number of initial protocol messages allowed by configured RBAC rules
  authorization_denied, Counter, Number of initial protocol messages denied by configured RBAC rules
  protocol_rejected, Counter, Number of connections closed due to invalid startup packets or encryption negotiation


.. _config_network_filters_postgres_proxy_dynamic_metadata:

Dynamic Metadata
----------------

When RBAC rules are configured, startup key/value pairs are exposed as string-valued dynamic metadata
under the ``envoy.filters.network.postgres_proxy`` namespace. All attributes present in the
StartupMessage are included, including attributes other than ``user`` and ``database``. Empty values
are preserved. The effective ``database`` value defaults to ``user`` when omitted or empty, and
``user`` and ``database`` values are truncated to PostgreSQL's 63-byte name limit for authorization.
These parsing changes can also affect the database suffix in the SQL metadata keys below. Policies
that restrict access by database should also account for the ``replication`` startup attribute.
PostgreSQL ignores the database name for physical replication sessions.

The filter publishes a ``packet_type`` field under ``envoy.filters.network.postgres_proxy.rbac``
before evaluating the rules. Its value is ``startup_message``, ``cancel_request``, ``ssl_request``,
or ``gss_enc_request`` (when using ``LOG`` rules). Startup attributes cannot set fields in this
namespace. They are available only in the ``StartupMessage`` evaluation.

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
