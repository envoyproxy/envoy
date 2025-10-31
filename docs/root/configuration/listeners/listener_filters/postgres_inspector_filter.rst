.. _config_listener_filters_postgres_inspector:

Postgres Inspector
==================

Postgres Inspector listener filter detects PostgreSQL protocol and cooperates with TLS Inspector
to enable :ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>` matching by SNI
and protocol. For SSL connections, it sends ``'S'`` response to ``SSLRequest``, enabling TLS
Inspector to extract SNI from the subsequent ClientHello for routing. For plaintext connections,
it extracts connection metadata (user, database, application_name) as typed dynamic metadata.

This filter works with all PostgreSQL versions and supports ``SSLRequest``, ``CancelRequest``,
and startup message parsing.

.. note::

  This filter should be placed before TLS Inspector in the listener filter chain.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector>`

Example
-------

A sample filter configuration could be:

.. code-block:: yaml

  listener_filters:
  - name: envoy.filters.listener.postgres_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
      enable_metadata_extraction: true
      max_startup_message_size: 10000
      startup_timeout: 10s
  - name: envoy.filters.listener.tls_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector

SNI-based routing example:

.. code-block:: yaml

  listeners:
  - address:
      socket_address:
        address: 0.0.0.0
        port_value: 5432
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
          stat_prefix: postgres_db1
          cluster: postgres_cluster_1
    - filter_chain_match:
        server_names: ["db2.example.com"]
        transport_protocol: "tls"
      filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: postgres_db2
          cluster: postgres_cluster_2

Statistics
----------

This filter has a statistics tree rooted at *postgres_inspector* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  postgres_found, Counter, Total PostgreSQL connections detected
  postgres_not_found, Counter, Total non-PostgreSQL connections
  ssl_requested, Counter, Total ``SSLRequest`` messages received
  ssl_not_requested, Counter, Total plaintext startup messages received
  ssl_response_success, Counter, Total successful ``'S'`` responses sent
  ssl_response_failed, Counter, Total failed SSL response writes
  protocol_violation, Counter, Total protocol violations
  startup_message_too_large, Counter, Total startup messages exceeding size limit
  startup_message_timeout, Counter, Total startup message timeouts
  protocol_error, Counter, Total malformed messages
  bytes_processed, Histogram, Number of bytes processed per connection

Dynamic Metadata
----------------

When ``enable_metadata_extraction`` is ``true``, the filter emits typed metadata under the
namespace ``envoy.postgres_inspector`` for plaintext connections with the type URL
``type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.StartupMetadata``.

Fields: ``user``, ``database``, ``application_name``.
