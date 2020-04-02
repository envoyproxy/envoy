.. _config_http_filters_grpc_stats:

gRPC Statistics
===============

* gRPC :ref:`architecture overview <arch_overview_grpc>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.grpc_stats.v2alpha.FilterConfig>`
* This filter should be configured with the name *envoy.filters.http.grpc_stats*.
* This filter can be enabled to emit a :ref:`filter state object
  <envoy_api_msg_config.filter.http.grpc_stats.v2alpha.FilterObject>`

This is a filter which enables telemetry of gRPC calls. Additionally, the
filter detects message boundaries in streaming gRPC calls and emits the message
counts for both the request and the response. 

More info: wire format in `gRPC over HTTP/2 <https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md>`_.

The filter emits statistics in the *cluster.<route target cluster>.grpc.* namespace. Depending on the
configuration, the stats may be prefixed with `<grpc service>.<grpc method>.`; the stats in the table below
are shown in this form. See the documentation for
:ref:`individual_method_stats_allowlist <envoy_api_field_config.filter.http.grpc_stats.v2alpha.FilterConfig.individual_method_stats_allowlist>`
and :ref:`stats_for_all_methods <envoy_api_field_config.filter.http.grpc_stats.v2alpha.FilterConfig.stats_for_all_methods>`.


.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  <grpc service>.<grpc method>.success, Counter, Total successful service/method calls
  <grpc service>.<grpc method>.failure, Counter, Total failed service/method calls
  <grpc service>.<grpc method>.total, Counter, Total service/method calls
  <grpc service>.<grpc method>.request_message_count, Counter, Total request message count for service/method calls
  <grpc service>.<grpc method>.response_message_count, Counter, Total response message count for service/method calls
