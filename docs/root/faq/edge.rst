.. _faq_edge:

Can I use Envoy as an edge proxy?
=================================

Envoy is a production-ready edge proxy, and performs well in that role, however,
the default settings are tailored for the service mesh use case, and some values
need to be adjusted when using Envoy as an edge proxy.

TCP proxies should configure: :ref:`overload_manager <config_overload_manager>`,
:ref:`listener buffer limits <envoy_api_field_Listener.per_connection_buffer_limit_bytes>` to 32,768 bytes
and :ref:`cluster buffer limits <envoy_api_field_Cluster.per_connection_buffer_limit_bytes>` to 32,768 bytes.

HTTP proxies should additionally configure:
:ref:`use_remote_address <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.use_remote_address>` to true,
and HTTP/2 settings (e.g.
:ref:`HTTP/2 maximum concurrent streams limit <envoy_api_field_core.Http2ProtocolOptions.max_concurrent_streams>` to 100,
:ref:`HTTP/2 initial stream window size limit <envoy_api_field_core.Http2ProtocolOptions.initial_stream_window_size>` to 65,535 bytes,
and :ref:`HTTP/2 initial connection window size limit <envoy_api_field_core.Http2ProtocolOptions.initial_connection_window_size>` to 1,048,576 bytes).

The following is a YAML example of the above recommendation.

.. code-block:: yaml

  overload_manager:
    refresh_interval:
      seconds: 0
      nanos: 250000000 # 0.25s
    resource_monitors:
    - name: "envoy.resource_monitors.fixed_heap"
      config:
        # TODO: Tune for your system.
        max_heap_size_bytes: 2147483648 # 2GB
    actions:
    - name: "envoy.overload_actions.stop_accepting_requests"
      triggers:
      - name: "envoy.resource_monitors.fixed_heap"
        threshold:
          value: 0.95

  static_resources:
    listeners:
    - address:
        socket_address: { address: 127.0.0.1, port_value: 443 }
      listener_filters:
      - name: "envoy.listener.tls_inspector"
        typed_config: {}
      per_connection_buffer_limit_bytes: 32768 # 32kB
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
          typed_config:
            "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
            stat_prefix: ingress_http
            use_remote_address: true
            http2_protocol_options:
              max_concurrent_streams: 100
              initial_stream_window_size: 65535 # 64kB
              initial_connection_window_size: 1048576 # 1MB
            route_config:
              virtual_hosts:
              - name: default
                domains: "*"
                routes:
                - match: { prefix: "/" }
                  route: { cluster: service_foo }
    clusters:
      name: service_foo
      connect_timeout: 15s
      per_connection_buffer_limit_bytes: 32768 # 32kB
      hosts:
        socket_address:
          address: 127.0.0.1
          port_value: 8080
      http2_protocol_options:
        max_concurrent_streams: 100
        initial_stream_window_size: 65535 # 64kB
        initial_connection_window_size: 1048576 # 1MB
