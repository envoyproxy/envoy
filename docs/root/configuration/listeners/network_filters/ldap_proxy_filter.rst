.. _config_network_filters_ldap_proxy:

LDAP proxy
==========

The LDAP proxy filter handles StartTLS upgrades transparently between an LDAP client (downstream)
and an LDAP server (upstream). It inspects the LDAP byte stream to detect StartTLS Extended
Requests and coordinates TLS negotiation on both sides of the connection using Envoy's existing
:ref:`StartTLS transport socket <extension_envoy.transport_sockets.starttls>`.

The filter works as an L4 proxy — it does not interpret LDAP message semantics beyond what is
needed for StartTLS detection. After TLS is established (or if the first message is a regular
LDAP operation), the filter transitions to passthrough mode and forwards all data without
inspection.

.. attention::

   The ``ldap_proxy`` filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

Operating modes
---------------

The filter supports two modes of operation:

* **ON_DEMAND** (default): The filter waits for the client to send a StartTLS Extended Request.
  When detected, it forwards the request upstream, waits for a success response, upgrades the
  upstream connection to TLS, sends the success response back to the client, and then upgrades
  the downstream connection to TLS.

* **ALWAYS**: The filter proactively sends a StartTLS request to the upstream server as soon as
  the first downstream data arrives. The downstream connection stays plaintext while the upstream
  is encrypted. This is useful when the LDAP client does not support StartTLS but the upstream
  server requires encryption.

Configuration
-------------

The LDAP proxy filter should be chained with the TCP proxy and configured with the StartTLS
transport socket:

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.ldap_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.ldap_proxy.v3alpha.LdapProxy
        stat_prefix: ldap
        upstream_starttls_mode: ON_DEMAND
    - name: envoy.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: tcp
        cluster: ldap_cluster
  transport_socket:
    name: envoy.transport_sockets.starttls
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.starttls.v3.StartTlsConfig
      cleartext_socket_config: {}
      tls_socket_config:
        common_tls_context:
          tls_certificates:
          - certificate_chain: { filename: "/certs/server.crt" }
            private_key: { filename: "/certs/server.key" }

.. _config_network_filters_ldap_proxy_stats:

Statistics
----------

Every configured LDAP proxy filter has statistics rooted at ``ldap.<stat_prefix>.`` with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 2, 1, 2

  starttls_req_total, Counter, Total number of StartTLS Extended Requests initiated
  starttls_rsp_total, Counter, Total number of StartTLS Extended Responses received
  starttls_rsp_success, Counter, Number of successful StartTLS responses (resultCode=0)
  starttls_rsp_error, Counter, Number of failed StartTLS responses (non-zero resultCode or parse/timeout errors)
  decoder_error, Counter, Number of protocol parsing errors
  protocol_violation, Counter, Number of pipeline violations (data sent after StartTLS before TLS handshake completes)
  starttls_op_time, Histogram, "Time taken for StartTLS operation from request to completion, in milliseconds"
