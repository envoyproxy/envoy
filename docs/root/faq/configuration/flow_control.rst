How do I configure flow control?
================================

Flow control may cause problems where Envoy is using non-streaming L7 filters, and request or
response bodies exceed the L7 buffer limits. For requests where the body must be buffered and
exceeds the configured limits, Envoy will serve a 413 to the user and increment the
:ref:`downstream_rq_too_large <config_http_conn_man_stats>` metric. On the response path if the
response body must be buffered and exceeds the limit, Envoy will increment the
:ref:`rs_too_large <config_http_conn_man_stats>` metric and either disconnect mid-response
(if headers have already been sent downstream) or send a 500 response.

There are three knobs for configuring Envoy flow control:
:ref:`listener limits <envoy_api_field_Listener.per_connection_buffer_limit_bytes>`,
:ref:`cluster limits <envoy_api_field_Cluster.per_connection_buffer_limit_bytes>` and
:ref:`http2 stream limits <envoy_api_field_core.Http2ProtocolOptions.initial_connection_window_size>`

The listener limits apply to how much raw data will be read per read() call from
downstream, as well as how much data may be buffered in userspace between Envoy
and downstream.

The listener limits are also propogated to the HttpConnectionManager, and applied on a per-stream
basis to HTTP/1.1 L7 buffers described below. As such they limit the size of HTTP/1 requests and
response bodies that can be buffered. For HTTP/2, as many streams can be multiplexed over one TCP
connection, the L7 and L4 buffer limits can be tuned separately, and the configuration option
:ref:`http2 stream limits <envoy_api_field_core.Http2ProtocolOptions.initial_connection_window_size>`
is applied to all of the L7 buffers. Note that for both HTTP/1 and
HTTP/2 Envoy can and will proxy arbitrarily large bodies on routes where all L7 filters are
streaming, but many filters such as the transcoder or buffer filters require the full HTTP body to
be buffered, so limit the request and response size based on the listener limit.

The cluster limits affect how much raw data will be read per read() call from upstream, as
well as how much data may be buffered in userspace between Envoy and upstream.

The following code block shows how to adjust all three fields mentioned above, though generally
the only one which needs to be amended is the listener
:ref:`per_connection_buffer_limit_bytes <envoy_api_field_Listener.per_connection_buffer_limit_bytes>`

.. code-block:: yaml

  staticResources:
    listeners:
      name: http
      address:
        socketAddress:
          address: '::1'
          portValue: 0
      filterChains:
        filters:
          name: envoy.http_connection_manager
          config:
            http2_protocol_options:
              initial_stream_window_size: 65535
            route_config: {}
            codec_type: HTTP2
            http_filters: []
            stat_prefix: config_test
      perConnectionBufferLimitBytes: 1024
    clusters:
      name: cluster_0
      connectTimeout: 5s
      perConnectionBufferLimitBytes: 1024
      hosts:
        socketAddress:
          address: '::1'
          portValue: 46685
