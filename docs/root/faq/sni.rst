.. _faq_how_to_setup_sni:

How do I setup SNI?
===================

`SNI <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ is only supported in the :ref:`v2
configuration/API <config_overview_v2>`.

.. attention::

  :ref:`TLS Inspector <config_listener_filters_tls_inspector>` listener filter must be configured
  in order to detect requested SNI.

The following is a YAML example of the above requirement.

.. code-block:: yaml

  address:
    socket_address: { address: 127.0.0.1, port_value: 1234 }
  listener_filters:
  - name: "envoy.listener.tls_inspector"
    config: {}
  filter_chains:
  - filter_chain_match:
      server_names: ["example.com", "www.example.com"]
    tls_context:
      common_tls_context:
        tls_certificates:
        - certificate_chain: { filename: "example_com_cert.pem" }
          private_key: { filename: "example_com_key.pem" }
    filters:
    - name: envoy.http_connection_manager
      config:
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
    tls_context:
      common_tls_context:
        tls_certificates:
        - certificate_chain: { filename: "api_example_com_cert.pem" }
          private_key: { filename: "api_example_com_key.pem" }
    filters:
    - name: envoy.http_connection_manager
      config:
        stat_prefix: ingress_http
        route_config:
          virtual_hosts:
          - name: default
            domains: "*"
            routes:
            - match: { prefix: "/" }
              route: { cluster: service_foo }
