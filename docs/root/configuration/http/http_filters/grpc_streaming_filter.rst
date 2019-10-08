.. _config_http_filters_grpc_web:

gRPC Streaming
========

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* :ref:`v2 API reference <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpFilter.name>`
* This filter should be configured with the name *envoy.filters.http.grpc_streaming*.

This is a filter which enables telemetry of gRPC streaming calls. The filter
detects message boundaries in the streaming gRPC calls and emits the message
counts for both the request and the response.  The filter emits statistics in
the *cluster.<route target cluster>.grpc.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  <grpc service>.<grpc method>.request_count, Counter, Total request message count for service/method calls
  <grpc service>.<grpc method>.request_count, Counter, Total response message count for service/method calls
