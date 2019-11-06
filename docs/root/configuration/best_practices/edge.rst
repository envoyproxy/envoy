.. _best_practices_edge:

Configuring Envoy as an edge proxy
==================================

Envoy is a production-ready edge proxy, however, the default settings are tailored
for the service mesh use case, and some values need to be adjusted when using Envoy
as an edge proxy.

TCP proxies should configure:

* restrict access to the admin endpoint,
* :ref:`overload_manager <config_overload_manager>`,
* :ref:`listener buffer limits <envoy_api_field_Listener.per_connection_buffer_limit_bytes>` to 32 KiB,
* :ref:`cluster buffer limits <envoy_api_field_Cluster.per_connection_buffer_limit_bytes>` to 32 KiB.

HTTP proxies should additionally configure:

* :ref:`use_remote_address <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.use_remote_address>`
  to true (to avoid consuming HTTP headers from external clients, see :ref:`HTTP header sanitizing <config_http_conn_man_header_sanitizing>`
  for details),
* :ref:`connection and stream timeouts <faq_configuration_timeouts>`,
* :ref:`HTTP/2 maximum concurrent streams limit <envoy_api_field_core.Http2ProtocolOptions.max_concurrent_streams>` to 100,
* :ref:`HTTP/2 initial stream window size limit <envoy_api_field_core.Http2ProtocolOptions.initial_stream_window_size>` to 64 KiB,
* :ref:`HTTP/2 initial connection window size limit <envoy_api_field_core.Http2ProtocolOptions.initial_connection_window_size>` to 1 MiB.

The following is a YAML example of the above recommendation.

.. code-block:: yaml

  overload_manager:
    refresh_interval: 0.25s
    resource_monitors:
    - name: "envoy.resource_monitors.fixed_heap"
      config:
        # TODO: Tune for your system.
        max_heap_size_bytes: 2147483648 # 2 GiB
    actions:
    - name: "envoy.overload_actions.shrink_heap"
      triggers:
      - name: "envoy.resource_monitors.fixed_heap"
        threshold:
          value: 0.95
    - name: "envoy.overload_actions.stop_accepting_requests"
      triggers:
      - name: "envoy.resource_monitors.fixed_heap"
        threshold:
          value: 0.98

  admin:
    access_log_path: "/var/log/envoy_admin.log"
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 9090

  static_resources:
    listeners:
    - address:
        socket_address:
          address: 0.0.0.0
          port_value: 443
      listener_filters:
      - name: "envoy.listener.tls_inspector"
        typed_config: {}
      per_connection_buffer_limit_bytes: 32768 # 32 KiB
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
            # Uncomment if Envoy is behind a load balancer that exposes client IP address using the PROXY protocol.
            # use_proxy_proto: true
            common_http_protocol_options:
              idle_timeout: 3600s # 1 hour
            http2_protocol_options:
              max_concurrent_streams: 100
              initial_stream_window_size: 65536 # 64 KiB
              initial_connection_window_size: 1048576 # 1 MiB
            stream_idle_timeout: 300s # 5 mins, must be disabled for long-lived and streaming requests
            request_timeout: 300s # 5 mins, must be disabled for long-lived and streaming requests
            route_config:
              virtual_hosts:
              - name: default
                domains: "*"
                routes:
                - match: { prefix: "/" }
                  route:
                    cluster: service_foo
                    idle_timeout: 15s # must be disabled for long-lived and streaming requests
    clusters:
      name: service_foo
      connect_timeout: 15s
      per_connection_buffer_limit_bytes: 32768 # 32 KiB
      hosts:
        socket_address:
          address: 127.0.0.1
          port_value: 8080
      http2_protocol_options:
        initial_stream_window_size: 65536 # 64 KiB
        initial_connection_window_size: 1048576 # 1 MiB
