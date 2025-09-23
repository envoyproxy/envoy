.. _config_listener_filters_postgres_inspector:

Postgres Inspector
==================

Status: contrib/experimental.

The Postgres Inspector listener filter detects PostgreSQL client connections and cooperates with
TLS Inspector to enable filter chain matching by protocol, and optionally by SNI when direct SSL
is used. It is a passive inspector: it does not reply to ``SSLRequest`` with ``'S'`` or ``'N'``
and does not perform TLS negotiation. When a plaintext startup message is observed, it extracts
connection metadata such as user, database, and application name.

.. tip::

   - Typed config: ``envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector``.
   - Category: ``envoy.filters.listener``.
   - Security posture: requires_trusted_downstream_and_upstream.

Overview
--------

- On reception of a PostgreSQL ``SSLRequest`` (Int32(8) + Int32(80877103)), the filter marks the
  connection transport protocol as ``postgres``, increments the ``ssl_requested`` and
  ``postgres_found`` counters, drains the 8-byte request, and immediately continues the listener
  filter chain.

- With PostgreSQL 17 and later, clients may operate in direct SSL mode and send TLS ``ClientHello``
  immediately after ``SSLRequest``. In this case, TLS Inspector can extract SNI for routing.

- In earlier PostgreSQL versions, the client expects an ``'S'``/``'N'`` response before sending
  ``ClientHello``. Because this filter is passive and does not send responses, SNI-based routing is
  only effective with direct SSL (PostgreSQL 17+).

- When the first bytes are not an ``SSLRequest``, the filter attempts to parse a PostgreSQL startup message:
  - Validates the length prefix and protocol version (3.0, value 196608).
  - On success, sets the connection transport protocol to ``postgres``, updates counters, and optionally extracts startup parameters.
  - If the message is incomplete, the filter waits for more bytes up to a configured maximum size.
  - If the message is too large or malformed, the filter updates error counters and may close the connection.

- If a ``CancelRequest`` (Int32(16) + Int32(80877102) + Int32(pid) + Int32(secret)) is observed,
  the filter marks protocol as ``postgres`` and continues. The message is unencrypted and does not
  carry startup parameters.

This filter should be placed before TLS Inspector in the listener's ``listener_filters`` so it can drain the ``SSLRequest`` and let TLS Inspector see the TLS ClientHello (when present). Example ordering:

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

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  postgres_found, Counter, Total number of times a PostgreSQL connection was positively detected.
  postgres_not_found, Counter, Total number of times data was determined not to be PostgreSQL.
  ssl_requested, Counter, Total number of times an ``SSLRequest`` was observed.
  ssl_not_requested, Counter, Total number of times a plaintext startup message was observed.
  startup_message_too_large, Counter, Total number of times the startup message exceeded the configured limit.
  startup_message_timeout, Counter, Total number of times the startup message was not received within timeout.
  protocol_error, Counter, Total number of malformed or invalid protocol messages observed.
  bytes_processed, Histogram, Records the number of bytes the inspector consumed while analyzing the connection. For plaintext startup this is the startup message length. For direct SSL, this is the 8-byte ``SSLRequest`` header. Nothing is recorded if insufficient bytes were available to make a determination.

Dynamic Metadata
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

SNI-based routing with direct SSL (PostgreSQL 17+):

.. code-block:: yaml

  listeners:
  - name: listener_0
    address:
      socket_address: { address: 0.0.0.0, port_value: 15432 }
    # Place Postgres Inspector before TLS Inspector so it can drain SSLRequest.
    listener_filters:
    - name: envoy.filters.listener.postgres_inspector
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
    - name: envoy.filters.listener.tls_inspector
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
    filter_chains:
    - filter_chain_match:
        server_names: ["db1.example.com"]
        transport_protocol: "tls"
      filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_db1
          cluster: pg_db1
    - filter_chain_match:
        server_names: ["db2.example.com"]
        transport_protocol: "tls"
      filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_db2
          cluster: pg_db2

Limitations
-----------

- The filter does not perform TLS negotiation or inspect encrypted payloads. It only drains the
  ``SSLRequest`` and delegates TLS handshake and SNI extraction to TLS Inspector.
- Startup parameter extraction occurs only for plaintext startup messages. If the client negotiates TLS, parameters are not extracted by this filter.
- SQL parsing and statement-level metadata are out of scope. See the :ref:`Postgres network filter <envoy_v3_api_msg_extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy>` if deeper protocol parsing is required.
