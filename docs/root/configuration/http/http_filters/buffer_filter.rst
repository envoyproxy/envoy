.. _config_http_filters_buffer:

Buffer
======

The buffer filter is used to stop filter iteration and wait for a fully buffered complete request.
This is useful in different situations including protecting some applications from having to deal
with partial requests and high network latency.

If enabled the buffer filter populates ``content-length`` header if it is not present in the request
already. The behavior can be disabled using the runtime feature
``envoy.reloadable_features.buffer_filter_populate_content_length``.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.buffer.v3.Buffer>`

Per-Route Configuration
-----------------------

The buffer filter configuration can be overridden or disabled on a per-route basis by providing a
:ref:`BufferPerRoute <envoy_v3_api_msg_extensions.filters.http.buffer.v3.BufferPerRoute>` configuration on
the virtual host, route, or weighted cluster.
