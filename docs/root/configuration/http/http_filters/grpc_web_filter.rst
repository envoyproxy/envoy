.. _config_http_filters_grpc_web:

gRPC-Web
========

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.grpc_web.v3.GrpcWeb``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.grpc_web.v3.GrpcWeb>`

This is a filter which enables the bridging of a gRPC-Web client to a compliant gRPC server by
following https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-WEB.md.
