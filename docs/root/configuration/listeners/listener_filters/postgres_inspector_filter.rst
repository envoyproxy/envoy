.. _config_listener_filters_postgres_inspector:

Postgres inspector
==================

Status: contrib/experimental.

The Postgres inspector listener filter detects PostgreSQL client connections and cooperates with
TLS Inspector to enable filter chain matching by SNI and protocol. It always passes SSL negotiation
to TLS Inspector and, when a plaintext startup message is observed, extracts connection metadata
such as user, database, and application name.

.. tip::

   - Typed config: ``envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector``.
   - Category: ``envoy.filters.listener``.
   - Security posture: requires_trusted_downstream_and_upstream.

Overview
--------

- On reception of a PostgreSQL ``SSLRequest`` (8 bytes), the filter:
  - Marks the connection transport protocol as ``postgres`` for filter chain matching.
  - Increments the ``postgres_inspector.ssl_requested`` and ``postgres_inspector.postgres_found`` counters.
  - Drains the 8-byte request and immediately continues the listener filter chain so that TLS Inspector can observe the TLS ClientHello and extract SNI.

- When the first bytes are not an ``SSLRequest``, the filter attempts to parse a PostgreSQL startup message:
  - Validates the length prefix and protocol version (3.0, value 196608).
  - On success, sets the connection transport protocol to ``postgres``, updates counters, and optionally extracts startup parameters.
  - If the message is incomplete, the filter waits for more bytes up to a configured maximum size.
  - If the message is too large or malformed, the filter updates error counters and may close the connection.

This filter should be placed before TLS Inspector in the listener's ``listener_filters`` so it can drain the SSLRequest and let TLS Inspector see the TLS ClientHello. Example ordering:

.. code-block:: yaml

  listener_filters:
  - name: envoy.filters.listener.postgres_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
  - name: envoy.filters.listener.tls_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector

Statistics
----------

All statistics are emitted with the ``postgres_inspector.`` prefix:

.. list-table::
  :header-rows: 1

  * - Name
    - Description
  * - ``postgres_found``
    - Incremented when a PostgreSQL connection is positively detected.
  * - ``postgres_not_found``
    - Incremented when data is determined not to be PostgreSQL.
  * - ``ssl_requested``
    - Incremented when an SSLRequest is observed.
  * - ``ssl_not_requested``
    - Incremented when a plaintext startup message is observed.
  * - ``startup_message_too_large``
    - Incremented when the startup message exceeds the configured limit.
  * - ``startup_message_timeout``
    - Incremented when the startup message is not received within timeout.
  * - ``protocol_error``
    - Incremented on malformed or invalid protocol messages.
  * - ``bytes_processed`` (hist.)
    - Histogram of total bytes consumed by the inspector per connection.

Dynamic metadata
----------------

When ``enable_metadata_extraction`` is ``true`` and a plaintext startup message is parsed successfully, the filter emits typed dynamic metadata under the namespace ``envoy.postgres_inspector``.

- Type URL: ``type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.StartupMetadata``.
- Fields:

  - ``user``: The username provided by the client.
  - ``database``: The database name. Defaults to the username when omitted.
  - ``application_name``: The application identifier provided by the client.

This typed metadata is available to downstream filters and access loggers. For example, Lua or external processing filters can read typed metadata from StreamInfo.

Examples
--------

Minimal listener with Postgres inspection and TCP proxy:

.. code-block:: yaml

  listeners:
  - name: listener_0
    address:
      socket_address: { address: 0.0.0.0, port_value: 15432 }
    listener_filters:
    - name: envoy.filters.listener.postgres_inspector
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
        enable_metadata_extraction: true
    - name: envoy.filters.listener.tls_inspector
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
    filter_chains:
    - filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: postgres_tcp
          cluster: upstream_pg

Limitations
-----------

- The filter does not perform TLS negotiation or inspect encrypted payloads. It only drains the SSLRequest and delegates TLS handshake and SNI extraction to TLS Inspector.
- Startup parameter extraction occurs only for plaintext startup messages. If the client negotiates TLS, parameters are not extracted by this filter.
- SQL parsing and statement-level metadata are out of scope. See the :ref:`Postgres network filter <envoy_v3_api_msg_extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy>` if deeper protocol parsing is required.
