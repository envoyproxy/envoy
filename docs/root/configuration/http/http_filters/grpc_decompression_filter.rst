.. _config_http_filters_grpc_decompression:

gRPC Decompression
================

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.grpc_decompressor.v3.Decompressor``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.grpc_decompressor.v3.Decompressor>`

The gRPC decompression filter enables decompression of messages following the `gRPC over HTTP/2 specification
<https://github.com/grpc/grpc/blob/345f048b5bebda1e03eb1c520ee0e18d7a694d11/doc/PROTOCOL-HTTP2.md#introduction>`_.
