.. _faq_how_to_setup_sni:

How do I setup SNI?
===================

`SNI <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ is only supported in the :ref:`v2
configuration/API <config_overview_v2>`.

The current implementation has the requirement that the :ref:`filters
<envoy_api_field_listener.FilterChain.filters>` in every :ref:`FilterChain <envoy_api_msg_listener.FilterChain>` must
be identical. In a future release, this requirement will be relaxed so that SNI can be used to
choose between completely different filter chains. :ref:`Domain name matching
<envoy_api_field_route.VirtualHost.domains>` can still be used within the HTTP connection manager to
choose different routes. This is by far the most common use case for SNI.

The following is a YAML example of the above requirement.

.. code-block:: yaml

  address:
    socket_address: { address: 127.0.0.1, port_value: 1234 }
  filter_chains:
  - filter_chain_match:
      sni_domains: "example.com"
    tls_context:
      common_tls_context:
        tls_certificates:
          - certificate_chain: { filename: "example_com_cert.pem" }
            private_key: { filename: "example_com_key.pem" }
    filters:
    - name: envoy.http_connection_manager
      config:
        route_config:
          virtual_hosts:
          - routes:
            - match: { prefix: "/" }
              route: { cluster: service_foo }
  - filter_chain_match:
      sni_domains: "www.example.com"
    tls_context:
      common_tls_context:
        tls_certificates:
          - certificate_chain: { filename: "www_example_com_cert.pem" }
            private_key: { filename: "www_example_com_key.pem" }
    filters:
    - name: envoy.http_connection_manager
      config:
        route_config:
          virtual_hosts:
          - routes:
            - match: { prefix: "/" }
              route: { cluster: service_foo }
