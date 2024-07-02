.. _faq_how_to_setup_sni:

How do I configure SNI for listeners?
=====================================

`SNI <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ is only supported in the :ref:`v3
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
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
  filter_chains:
  - filter_chain_match:
      server_names: ["example.com", "www.example.com"]
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
        common_tls_context:
          tls_certificates:
          - certificate_chain: { filename: "example_com_cert.pem" }
            private_key: { filename: "example_com_key.pem" }
    filters:
    - name: envoy.filters.network.http_connection_manager
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
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
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
        common_tls_context:
          tls_certificates:
          - certificate_chain: { filename: "api_example_com_cert.pem" }
            private_key: { filename: "api_example_com_key.pem" }
    filters:
    - name: envoy.filters.network.http_connection_manager
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
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

For clusters, a fixed SNI can be set in :ref:`sni <envoy_v3_api_field_extensions.transport_sockets.tls.v3.UpstreamTlsContext.sni>`.
To derive SNI from a downstream HTTP header like, ``host`` or ``:authority``, turn on
:ref:`auto_sni <envoy_v3_api_field_config.core.v3.UpstreamHttpProtocolOptions.auto_sni>` to override the fixed SNI in
:ref:`UpstreamTlsContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.UpstreamTlsContext>`. A custom header other than the ``host`` or ``:authority`` can also be supplied using the optional
:ref:`override_auto_sni_header <envoy_v3_api_field_config.core.v3.UpstreamHttpProtocolOptions.override_auto_sni_header>` field.
Alternatively :ref:`hostnames <envoy_v3_api_field_config.endpoint.v3.Endpoint.hostname>` of cluster's endpoints can be used as the value for SNI.
Turn on :ref:`auto_sni_from_upstream <envoy_v3_api_field_config.core.v3.UpstreamHttpProtocolOptions.auto_sni_from_upstream>` to enable this mechanism.
If upstream will present certificates with the hostname in SAN, turn on
:ref:`auto_san_validation <envoy_v3_api_field_config.core.v3.UpstreamHttpProtocolOptions.auto_san_validation>` too.
It still needs a trust CA in validation context in :ref:`UpstreamTlsContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.UpstreamTlsContext>` for trust anchor.
