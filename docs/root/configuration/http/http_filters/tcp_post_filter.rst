.. _config_http_filters_tcp_post:

TCP Post
===============

The TcpPost filter is used by the upstream proxy to coordinate with downstream proxy that proxies
TCP streams over HTTP/2 POST requests. 

Configuration
-------------

* This filter should be configured with the name *envoy.filters.http.tcp_post*.


It converts POST requests back to CONNECT to trigger the regular TCP decapping. Therefore, it's
typically used with another localhost listener that does the regular TCP decapping.

The following is an example configuration:

.. code-block:: yaml

static_resources:
  listeners:
  - name: listener_1
    address:
      socket_address:
        protocol: TCP
        address: 0.0.0.0
        port_value: 10001
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress_http
          route_config:
            name: local_route
            virtual_hosts:
            - name: local_service
              domains:
                - "*"
              routes:
                - match:
                    prefix: "/"
                  route:
                    cluster: cluster_1
          http_filters:
          - name: envoy.filters.http.tcp_post
          -  typed_config:
               "@type": type.googleapis.com/envoy.extensions.filters.http.tcp_post.v3.TcpPost
               headers:
	         name: ":method"
                 exact_match: "POST"
          - name: envoy.filters.http.router

  - name: listener_2
    address:
      socket_address:
        protocol: TCP
        address: 127.0.0.1
        port_value: 10002
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress_http
          route_config:
            name: local_route
            virtual_hosts:
            - name: local_service
              domains:
                - "*"
              routes:
                - match:
                    connect_matcher:
                      {}
                  route:
                    cluster: cluster_2
                    upgrade_configs:
                      - upgrade_type: CONNECT
                        connect_config:
                          {}
          http_filters:
          - name: envoy.filters.http.router
          http2_protocol_options:
            allow_connect: true
          upgrade_configs:
            - upgrade_type: CONNECT

  - name: listener_3
    address:
      socket_address:
        protocol: TCP
        address: 127.0.0.1
        port_value: 10003
    filter_chains:
    - filters:
      - name: tcp
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: "tcp_upstream"

  clusters:
  - name: cluster_1
    connect_timeout: 2s
    http2_protocol_options:
      allow_connect: true
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 10002

  - name: tcp_upstream
    connect_timeout: 2s
    load_assignment:
      cluster_name: tcp_upstream
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 10003


