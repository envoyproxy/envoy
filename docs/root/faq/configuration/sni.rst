.. _faq_how_to_setup_sni:

How do I configure SNI for listeners?
=====================================

`SNI <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ is only supported in the :ref:`v2
configuration/API <config_overview>`.

.. attention::

  :ref:`TLS Inspector <config_listener_filters_tls_inspector>` listener filter must be configured
  in order to detect requested SNI.

The following is a YAML example of the above requirement.

.. code-block:: yaml

  address:
    socket_address: { address: 127.0.0.1, port_value: 1234 }
  listener_filters:
  - name: "envoy.filters.listener.tls_inspector"
    typed_config: {}
  filter_chains:
  - filter_chain_match:
      server_names: ["example.com", "www.example.com"]
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
        common_tls_context:
          tls_certificates:
          - certificate_chain: { filename: "example_com_cert.pem" }
            private_key: { filename: "example_com_key.pem" }
    filters:
    - name: envoy.filters.network.http_connection_manager
      typed_config:
        "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
        stat_prefix: ingress_http
        route_config:
          virtual_hosts:
          - name: default
            domains: "*"
            routes:
            - match: { prefix: "/" }
              route: { cluster: service_foo }
  - filter_chain_match:
      server_names: "api.example.com"
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.api.v2.auth.DownstreamTlsContext
        common_tls_context:
          tls_certificates:
          - certificate_chain: { filename: "api_example_com_cert.pem" }
            private_key: { filename: "api_example_com_key.pem" }
    filters:
    - name: envoy.filters.network.http_connection_manager
      typed_config:
        "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
        stat_prefix: ingress_http
        route_config:
          virtual_hosts:
          - name: default
            domains: "*"
            routes:
            - match: { prefix: "/" }
              route: { cluster: service_foo }


How do I configure SNI for clusters?
====================================

For clusters, a fixed SNI can be set in :ref:`UpstreamTlsContext <envoy_api_field_auth.UpstreamTlsContext.sni>`.
To derive SNI from HTTP `host` or `:authority` header, turn on
:ref:`auto_sni <envoy_api_field_core.UpstreamHttpProtocolOptions.auto_sni>` to override the fixed SNI in
`UpstreamTlsContext`. If upstream will present certificates with the hostname in SAN, turn on
:ref:`auto_san_validation <envoy_api_field_core.UpstreamHttpProtocolOptions.auto_san_validation>` too.
It still needs a trust CA in validation context in `UpstreamTlsContext` for trust anchor.
