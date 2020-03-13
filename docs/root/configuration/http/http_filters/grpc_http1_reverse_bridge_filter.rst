.. _config_http_filters_grpc_http1_reverse_bridge:

gRPC HTTP/1.1 reverse bridge
============================

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.grpc_http1_reverse_bridge.v2alpha1.FilterConfig>`
* This filter should be configured with the name *envoy.filters.http.grpc_http1_reverse_bridge*.

This is a filter that enables converting an incoming gRPC request into a HTTP/1.1 request to allow
a server that does not understand HTTP/2 or gRPC semantics to handle the request.

The filter works by:

* Checking the content type of the incoming request. If it's a gRPC request, the filter is enabled.
* The content type is modified to a configurable value. This can be a noop by configuring
  ``application/grpc``.
* The gRPC frame header is optionally stripped from the request body. The content length header
  will be adjusted if so.
* On receiving a response, the content type of the response is validated and the status code is
  mapped to a grpc-status which is inserted into the response trailers.
* The response body is optionally prefixed by the gRPC frame header, again adjusting the content
  length header if necessary.

Due to being mapped to HTTP/1.1, this filter will only work with unary gRPC calls.

gRPC frame header management
----------------------------

By setting the withhold_grpc_frame option, the filter will assume that the upstream does not
understand any gRPC semantics and will convert the request body into a simple binary encoding
of the request body and perform the reverse conversion on the response body. This ends up
simplifying the server side handling of these requests, as they no longer need to be concerned
with parsing and generating gRPC formatted data.

This works by stripping the gRPC frame header from the request body, while injecting a gRPC
frame header in the response.

If this feature is not used, the upstream must be ready to receive HTTP/1.1 requests prefixed
with the gRPC frame header and respond with gRPC formatted responses.

How to disable HTTP/1.1 reverse bridge filter per route
-------------------------------------------------------

.. code-block:: yaml

  admin:
    access_log_path: /dev/stdout
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 9901
  static_resources:
    listeners:
    - name: listener_0
      address:
        socket_address:
          address: 0.0.0.0
          port_value: 80
      filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
            access_log:
            - name: envoy.access_loggers.file
              typed_config:
                "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
                path: /dev/stdout
            stat_prefix: ingress_http
            route_config:
              name: local_route
              virtual_hosts:
              - name: local_service
                domains: ["*"]
                routes:
                - match:
                    prefix: "/route-with-filter-disabled"
                  route:
                    host_rewrite: localhost
                    cluster: grpc
                    timeout: 5.00s
                  # per_filter_config disables the filter for this route
                  per_filter_config:
                    envoy.filters.http.grpc_http1_reverse_bridge:
                      disabled: true
                - match:
                    prefix: "/route-with-filter-enabled"
                  route:
                    host_rewrite: localhost
                    cluster: other
                    timeout: 5.00s
            http_filters:
            - name: envoy.filters.http.grpc_http1_reverse_bridge
              typed_config:
                "@type": type.googleapis.com/envoy.config.filter.http.grpc_http1_reverse_bridge.v2alpha1.FilterConfig
                content_type: application/grpc+proto
                withhold_grpc_frames: true
            - name: envoy.filters.http.router
              typed_config: {}
    clusters:
    - name: other
      connect_timeout: 5.00s
      type: LOGICAL_DNS
      dns_lookup_family: V4_ONLY
      lb_policy: ROUND_ROBIN
      hosts:
        - socket_address:
            address: localhost
            port_value: 4630
    - name: grpc
      connect_timeout: 5.00s
      type: strict_dns
      lb_policy: round_robin
      http2_protocol_options: {}
      load_assignment:
        cluster_name: grpc
        endpoints:
          - lb_endpoints:
              - endpoint:
                  address:
                    socket_address:
                      address: localhost
                      port_value: 10005
