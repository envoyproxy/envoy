.. _faq_how_to_setup_sni:

如何为监听器配置 SNI？
=====================================

`SNI <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ 只在 :ref:`v3
配置/API <config_overview>` 中支持

.. attention::

  必须配置 :ref:`TLS 检查器 <config_listener_filters_tls_inspector>` 监听过滤器才能检测到所请求的 SNI 。

以下是上述要求的 YAML 示例。

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


如何为群集配置 SNI？
====================================

对于集群而言， 可以在 :ref:`UpstreamTlsContext <envoy_v3_api_field_extensions.transport_sockets.tls.v3.UpstreamTlsContext.sni>` 中设置固定的 SNI。
要从 HTTP `host` 或者 `:authority` 头派生 SNI, 请开启
:ref:`auto_sni <envoy_v3_api_field_config.core.v3.UpstreamHttpProtocolOptions.auto_sni>` 去覆盖
`UpstreamTlsContext` 中固定的 SNI。 如果上游需要在 SAN 中出示包含主机名的证书，请也打开
:ref:`auto_san_validation <envoy_v3_api_field_config.core.v3.UpstreamHttpProtocolOptions.auto_san_validation>` 。
在 `UpstreamTlsContext` 的验证上下文中，它仍需要信任的 CA 证书以用于认证。
