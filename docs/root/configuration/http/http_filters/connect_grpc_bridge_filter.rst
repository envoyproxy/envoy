.. _config_http_filters_connect_grpc_bridge:

Connect-gRPC Bridge
===================

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.connect_grpc_bridge.v3.FilterConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.connect_grpc_bridge.v3.FilterConfig>`

This filter enables a Buf Connect client to connect to a compliant gRPC server.
More information on the Buf Connect protocol can be found `here <https://connect.build/docs/protocol>`_.

HTTP GET support
----------------
This filter supports `Buf Connect HTTP GET requests <https://connect.build/docs/protocol#unary-get-request>`_. The
``Connect-Version-Query`` parameter must be specified in requests in order for them to be translated by this filter,
which will be done automatically when using a Connect implementation.
