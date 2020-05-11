.. _config_http_filters_grpc_stats:

gRPC Statistics
===============

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.grpc_stats.v3.FilterConfig>`
* This filter should be configured with the name *envoy.filters.http.grpc_stats*.
* This filter can be enabled to emit a :ref:`filter state object
  <envoy_v3_api_msg_extensions.filters.http.grpc_stats.v3.FilterObject>`

This is a filter which enables telemetry of gRPC calls. Additionally, the
filter detects message boundaries in streaming gRPC calls and emits the message
counts for both the request and the response. 

More info: wire format in `gRPC over HTTP/2 <https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md>`_.

The filter emits statistics in the *cluster.<route target cluster>.grpc.* namespace. Depending on the
configuration, the stats may be prefixed with `<grpc service>.<grpc method>.`; the stats in the table below
are shown in this form. See the documentation for
:ref:`individual_method_stats_allowlist <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.individual_method_stats_allowlist>`
and :ref:`stats_for_all_methods <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.stats_for_all_methods>`.

To enable *upstream_rq_time* (v3 API only) see :ref:`enable_upstream_stats <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.enable_upstream_stats>`.


.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  <grpc service>.<grpc method>.success, Counter, Total successful service/method calls
  <grpc service>.<grpc method>.failure, Counter, Total failed service/method calls
  <grpc service>.<grpc method>.total, Counter, Total service/method calls
  <grpc service>.<grpc method>.request_message_count, Counter, Total request message count for service/method calls
  <grpc service>.<grpc method>.response_message_count, Counter, Total response message count for service/method calls
  <grpc service>.<grpc method>.upstream_rq_time, Histogram, Request time milliseconds
