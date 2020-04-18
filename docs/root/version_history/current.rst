1.15.0 (Pending)
================

Changes
-------

* access loggers: added GRPC_STATUS operator on logging format.
* dynamic forward proxy: added :ref:`SNI based dynamic forward proxy <config_network_filters_sni_dynamic_forward_proxy>` support.
* fault: added support for controlling the percentage of requests that abort, delay and response rate limits faults 
  are applied to using :ref:`HTTP headers <config_http_filters_fault_injection_http_header>` to the HTTP fault filter.
* fault: added support for specifying grpc_status code in abort faults using
  :ref:`HTTP header <config_http_filters_fault_injection_http_header>` to the HTTP fault filter or
  :ref:`abort fault configuration <envoy_v3_api_field_extensions.filters.http.fault.v3.FaultAbort>`.
* filter: add `upstram_rq_time` stats to the GPRC stats filter.
  Disabled by default and can be enabled via :ref:`enable_upstream_stats <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.enable_upstream_stats>`.
* http: fixed a bug where the upgrade header was not cleared on responses to non-upgrade requests.
  Can be reverted temporarily by setting runtime feature `envoy.reloadable_features.fix_upgrade_response` to false.
* tracing: tracing configuration has been made fully dynamic and every HTTP connection manager
  can now have a separate :ref:`tracing provider <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing.provider>`.

Deprecated
----------

* Tracing provider configuration as part of :ref:`bootstrap config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.tracing>`
  has been deprecated in favor of configuration as part of :ref:`HTTP connection manager
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing.provider>`.
