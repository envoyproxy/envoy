.. _config_listener_filters_postgres_inspector:

Postgres Inspector
==================

Postgres Inspector listener filter allows detecting whether the application protocol appears to be
PostgreSQL, and if it is PostgreSQL, it extracts connection metadata for routing and observability.
This can be used to select a :ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>`
via the :ref:`transport_protocol <envoy_v3_api_field_config.listener.v3.FilterChainMatch.transport_protocol>`
of a :ref:`FilterChainMatch <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>`.

The filter detects PostgreSQL connections by inspecting the initial protocol handshake and marks
the connection with ``transport_protocol="postgres"`` for filter chain selection. For plaintext
connections, it can optionally extract metadata such as username, database name, and application
name from the startup message.

.. important::

  This is a **passive** inspector that only observes and detects the PostgreSQL protocol. It does
  **not** participate in SSL negotiation. For PostgreSQL SSL connections, the actual SSL negotiation
  must be handled by the :ref:`postgres_proxy <config_network_filters_postgres_proxy>` network
  filter which sends the 'S' response to ``SSLRequest`` messages.

.. note::

  For SNI-based routing of PostgreSQL connections:

  - **PostgreSQL 17+**: SNI-based filter chain selection works natively. PostgreSQL 17+ sends
    the SSLRequest and TLS ClientHello together in the initial stream, allowing the TLS Inspector
    to extract SNI before filter chain selection occurs.

  - **PostgreSQL < 17**: SNI-based filter chain selection is **not supported**. These versions
    use a two-phase SSL negotiation (SSLRequest → 'S' response → TLS handshake), which means
    the TLS handshake (and thus SNI extraction) occurs after filter chain selection. For these
    versions, use :ref:`Dynamic Forward Proxy <arch_overview_http_dynamic_forward_proxy>`
    to read SNI from connection metadata for routing decisions.

  The postgres_inspector should be placed **before** the :ref:`tls_inspector
  <config_listener_filters_tls_inspector>` filter in the listener filter chain for both cases.

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

Integration with postgres_proxy for SSL handling
-------------------------------------------------

The postgres_inspector detects PostgreSQL protocol, while postgres_proxy handles SSL negotiation:

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
        transport_protocol: "postgres"
      transport_socket:
        name: envoy.transport_sockets.tls
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
            - certificate_chain: { filename: "server_cert.pem" }
              private_key: { filename: "server_key.pem" }
      filters:
      - name: envoy.filters.network.postgres_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy
          stat_prefix: postgres
          terminate_ssl: true
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: postgres_tcp
          cluster: postgres_cluster

SNI-based routing with Dynamic Forward Proxy
---------------------------------------------

For SNI-based routing of PostgreSQL connections, use Dynamic Forward Proxy instead of filter chain
selection. The TLS Inspector extracts SNI after the TLS handshake completes, and Dynamic Forward
Proxy uses the SNI from connection metadata for routing:

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
        transport_protocol: "postgres"
      transport_socket:
        name: envoy.transport_sockets.tls
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
          common_tls_context:
            tls_certificates:
            - certificate_chain: { filename: "server_cert.pem" }
              private_key: { filename: "server_key.pem" }
      filters:
      - name: envoy.filters.network.postgres_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.postgres_proxy.v3alpha.PostgresProxy
          stat_prefix: postgres
          terminate_ssl: true
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: postgres_tcp
          cluster: dynamic_forward_proxy_cluster
          tunneling_config:
            hostname: "%REQUESTED_SERVER_NAME%"
  clusters:
  - name: dynamic_forward_proxy_cluster
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config:
          name: dynamic_forward_proxy_cache_config
          dns_lookup_family: V4_ONLY

Statistics
----------

This filter has a statistics tree rooted at *postgres_inspector* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  postgres_found, Counter, Total number of times PostgreSQL protocol was detected
  postgres_not_found, Counter, Total number of times PostgreSQL protocol was not detected
  ssl_requested, Counter, Total number of SSLRequest messages received
  ssl_not_requested, Counter, Total number of plaintext startup messages received
  startup_message_too_large, Counter, Total number of startup messages exceeding the size limit
  startup_message_timeout, Counter, Total number of connections that timed out waiting for startup message
  protocol_error, Counter, Total number of malformed or invalid PostgreSQL messages
  bytes_processed, Histogram, Number of bytes processed per connection during protocol detection

Dynamic Metadata
----------------

When ``enable_metadata_extraction`` is ``true``, the filter emits typed metadata under the
namespace ``envoy.postgres_inspector`` for plaintext connections with the type URL
``type.googleapis.com/envoy.extensions.filters.listener.postgres_inspector.v3alpha.StartupMetadata``.

Extracted fields include:

* ``user`` - PostgreSQL username from startup message.
* ``database`` - Database name from startup message.
* ``application_name`` - Application name from startup message if provided.

.. note::

  Metadata extraction only works for **plaintext** startup messages. For SSL connections, the
  startup message is encrypted after the TLS handshake completes, so metadata cannot be extracted
  by the listener filter.
