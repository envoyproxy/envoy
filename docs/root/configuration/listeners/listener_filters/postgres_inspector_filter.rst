.. _config_listener_filters_postgres_inspector:

Postgres Inspector
==================

Status: contrib/experimental.

The Postgres Inspector listener filter detects PostgreSQL client connections and cooperates with
TLS Inspector to enable filter chain matching by protocol and SNI-based routing. It actively
participates in SSL negotiation by sending ``'S'`` (accept SSL) response to ``SSLRequest``,
enabling TLS Inspector to extract SNI from the subsequent ClientHello. This works with all
PostgreSQL versions (< 17 and 17+). When a plaintext startup message is observed, it extracts
connection metadata such as user, database, and application name.

.. tip::

   - Typed config: ``envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector``.
   - Category: ``envoy.filters.listener``.
   - Security posture: requires_trusted_downstream_and_upstream.

Use Cases
---------

**Primary Use Cases:**

1. **SNI-based routing for PostgreSQL databases:** Route encrypted PostgreSQL connections to different
   backend clusters based on the SNI (Server Name Indication) in the TLS ClientHello. This enables
   hosting multiple PostgreSQL databases behind a single Envoy listener with routing based on hostname
   (e.g., ``db1.example.com`` → cluster_db1, ``db2.example.com`` → cluster_db2).

2. **Protocol-based filter chain matching:** Use the detected ``postgres`` transport protocol to select
   appropriate filter chains. This allows different processing for PostgreSQL traffic vs other protocols
   on the same port.

3. **Connection metadata extraction for observability:** Extract user, database, and application_name from
   plaintext startup messages and expose them as typed dynamic metadata. This metadata is available to:
   
   - **Access loggers:** Log which users/databases are connecting through Envoy
   - **External processing filters:** Make routing or policy decisions based on user/database
   - **Stats tagging:** Emit per-user or per-database metrics
   - **Cluster selection:** Use metadata in subset selectors for advanced routing

4. **PostgreSQL traffic detection:** Identify PostgreSQL protocol for observability, monitoring, and
   ensuring only PostgreSQL traffic reaches PostgreSQL-specific filter chains or clusters.

Overview
--------

**SSL Negotiation (All PostgreSQL Versions):**

- On reception of a PostgreSQL ``SSLRequest`` (Int32(8) + Int32(80877103)), the filter:
  
  1. Marks the connection transport protocol as ``postgres`` for filter chain matching.
  2. Increments the ``ssl_requested`` and ``postgres_found`` counters.
  3. Sends ``'S'`` (0x53) response to accept SSL, enabling TLS handshake.
  4. Drains the 8-byte ``SSLRequest`` and continues to TLS Inspector.
  5. TLS Inspector processes the ``ClientHello`` and extracts SNI for routing.

- This approach works with all PostgreSQL versions:
  
  - **PostgreSQL < 17:** Client waits for ``'S'`` response before sending ``ClientHello``.
  - **PostgreSQL 17+:** Client sends ``ClientHello`` immediately (direct SSL mode). The ``'S'`` response is harmless.

**Plaintext Connections:**

- When the first bytes are not an ``SSLRequest``, the filter attempts to parse a PostgreSQL startup message:
  - Validates the length prefix and protocol version (3.0, value 196608).
  - On success, sets the connection transport protocol to ``postgres``, updates counters, and optionally extracts startup parameters.
  - If the message is incomplete, the filter waits for more bytes up to a configured maximum size.
  - If the message is too large or malformed, the filter updates error counters and may close the connection.

**CancelRequest Handling:**

- If a ``CancelRequest`` (Int32(16) + Int32(80877102) + Int32(pid) + Int32(secret)) is observed,
  the filter marks protocol as ``postgres`` and passes it through. ``CancelRequest`` is sent on a
  separate connection to cancel a running query and is always plaintext (never encrypted). It does
  not carry startup parameters.

**Filter Ordering:**

This filter should be placed before TLS Inspector in the listener's ``listener_filters`` so it can handle SSL negotiation and prepare the stream for TLS handshake. Example ordering:

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
  ssl_response_success, Counter, Total number of times ``'S'`` response was successfully sent for ``SSLRequest``.
  ssl_response_failed, Counter, Total number of times sending ``'S'`` response failed (write error).
  protocol_violation, Counter, Total number of protocol violations detected (e.g. unencrypted data after ``SSLRequest``).
  startup_message_too_large, Counter, Total number of times the startup message exceeded the configured limit.
  startup_message_timeout, Counter, Total number of times the startup message was not received within timeout.
  protocol_error, Counter, Total number of malformed or invalid protocol messages observed.
  bytes_processed, Histogram, Records the number of bytes the inspector consumed while analyzing the connection. For plaintext startup this is the startup message length. For SSL connections this is 8 bytes (``SSLRequest``). Nothing is recorded if insufficient bytes were available to make a determination.

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

SNI-based routing (all PostgreSQL versions):

.. code-block:: yaml

  listeners:
  - name: listener_0
    address:
      socket_address: { address: 0.0.0.0, port_value: 15432 }
    # Place Postgres Inspector before TLS Inspector to handle SSL negotiation.
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

- The filter always sends ``'S'`` (accept SSL) in response to ``SSLRequest``. It does not support rejecting SSL or conditional SSL acceptance.
- Startup parameter extraction occurs only for plaintext startup messages. Parameters sent after TLS handshake completion cannot be extracted by this listener filter.
- The filter does not inspect encrypted payloads. All TLS handshake and encryption is handled by TLS Inspector and downstream filters.
- SQL parsing and statement-level metadata are out of scope. See the :ref:`Postgres network filter <envoy_v3_api_msg_extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy>` if deeper protocol parsing is required.
