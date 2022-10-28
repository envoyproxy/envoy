.. _config_http_filters_grpc_http1_reverse_bridge:

gRPC HTTP/1.1 reverse bridge
============================

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.grpc_http1_reverse_bridge.v3.FilterConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.grpc_http1_reverse_bridge.v3.FilterConfig>`

This is a filter that enables converting an incoming gRPC request into a HTTP/1.1 request to allow
a server that does not understand HTTP/2 or HTTP/3 or gRPC semantics to handle the request.

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

.. literalinclude:: _include/grpc-reverse-bridge-filter.yaml
    :language: yaml
