.. _config_http_filters_grpc_bridge:

gRPC HTTP/1.1 bridge
====================

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.grpc_http1_bridge.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.grpc_http1_bridge.v3.Config>`

This is a simple filter which enables the bridging of an HTTP/1.1 client which does not support
response trailers to a compliant gRPC server. It works by doing the following:

* When a request is sent, the filter sees if the connection is HTTP/1.1 and the request content type
  is ``application/grpc``.
* If so, when the response is received, the filter buffers it and waits for trailers and then checks the
  ``grpc-status`` code. If it is not zero, the filter switches the HTTP response code to 503. It also copies
  the ``grpc-status`` and ``grpc-message`` trailers into the response headers so that the client can look
  at them if it wishes.
* The client should send HTTP/1.1 requests that translate to the following pseudo headers:

  * ``:method``: POST
  * ``:path``: <gRPC-METHOD-NAME>
  * ``content-type``: application/grpc

* The body should be the serialized grpc body which is:

  * 1 byte of zero (not compressed).
  * network order 4 bytes of proto message length.
  * serialized proto message.

* Because this scheme must buffer the response to look for the ``grpc-status`` trailer it will only
  work with unary gRPC APIs.

This filter also collects stats for all gRPC requests that transit, even if those requests are
normal gRPC requests over HTTP/2 or above.

More info: wire format in `gRPC over HTTP/2 <https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md>`_.

.. attention::

   Note that statistics should be collected by the dedicated :ref:`gRPC stats filter
   <config_http_filters_grpc_stats>` instead. The use of this filter for gRPC telemetry
   has been disabled.

Protobuf upgrade support
------------------------

The filter will automatically frame requests with content-type ``application/x-protobuf`` as gRPC requests if
:ref:`upgrade_protobuf_to_grpc <envoy_v3_api_field_extensions.filters.http.grpc_http1_bridge.v3.Config.upgrade_protobuf_to_grpc>` is set.
In this case the filter will prepend the body with the gRPC frame described above, and update the content-type header to
``application/grpc`` before sending the request to the gRPC server.

In case the client sends a ``content-length`` header it will be removed before proceeding, as the value may conflict with
the size specified in the gRPC frame.

The response body returned to the client will not contain the gRPC header frame for requests that are upgraded in this
fashion, i.e. the body will contain only the encoded Protobuf.

Statistics
----------

The filter emits statistics in the ``cluster.<route target cluster>.grpc.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  <grpc service>.<grpc method>.success, Counter, Total successful service/method calls
  <grpc service>.<grpc method>.failure, Counter, Total failed service/method calls
  <grpc service>.<grpc method>.total, Counter, Total service/method calls
