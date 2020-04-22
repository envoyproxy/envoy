.. _config_http_filters_buffer:

Buffer
======

The buffer filter is used to stop filter iteration and wait for a fully buffered complete request.
This is useful in different situations including protecting some applications from having to deal
with partial requests and high network latency.

If enabled the buffer filter populates content-length header if it is not present in the request
already. The behavior can be disabled using the runtime feature
`envoy.reloadable_features.buffer_filter_populate_content_length`.

* :ref:`v2 API reference <envoy_api_msg_config.filter.http.buffer.v2.Buffer>`
* This filter should be configured with the name *envoy.filters.http.buffer*.

Per-Route Configuration
-----------------------

The buffer filter configuration can be overridden or disabled on a per-route basis by providing a
:ref:`BufferPerRoute <envoy_api_msg_config.filter.http.buffer.v2.BufferPerRoute>` configuration on
the virtual host, route, or weighted cluster.
