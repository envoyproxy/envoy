1.15.0 (Pending)
================

Changes
-------

* access loggers: added GRPC_STATUS operator on logging format.
* access loggers: applied existing buffer limits to the non-google gRPC access logs, as well as :ref:`stats <config_access_log_stats>` for logged / dropped logs.
* access loggers: extened specifier for FilterStateFormatter to output :ref:`unstructured log string <config_access_log_format_filter_state>`.
* config: added :ref:`version_text <config_cluster_manager_cds>` stat that reflects xDS version.
* dynamic forward proxy: added :ref:`SNI based dynamic forward proxy <config_network_filters_sni_dynamic_forward_proxy>` support.
* fault: added support for controlling the percentage of requests that abort, delay and response rate limits faults
  are applied to using :ref:`HTTP headers <config_http_filters_fault_injection_http_header>` to the HTTP fault filter.
* fault: added support for specifying grpc_status code in abort faults using
  :ref:`HTTP header <config_http_filters_fault_injection_http_header>` or abort fault configuration in HTTP fault filter.
* filter: add `upstram_rq_time` stats to the GPRC stats filter.
  Disabled by default and can be enabled via :ref:`enable_upstream_stats <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.enable_upstream_stats>`.
* grpc-json: added support for streaming response using
  `google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_.
* gzip filter: added option to set zlib's next output buffer size.
* health checks: allow configuring health check transport sockets by specifying :ref:`transport socket match criteria <envoy_v3_api_field_config.core.v3.HealthCheck.transport_socket_match_criteria>`.
* http: fixed a bug where in some cases slash was moved from path to query string when :ref:`merging of adjacent slashes<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.merge_slashes>` is enabled.
* http: fixed a bug where the upgrade header was not cleared on responses to non-upgrade requests.
  Can be reverted temporarily by setting runtime feature `envoy.reloadable_features.fix_upgrade_response` to false.
* http: remove legacy connection pool code and their runtime features: `envoy.reloadable_features.new_http1_connection_pool_behavior` and
  `envoy.reloadable_features.new_http2_connection_pool_behavior`.
* listener: added in place filter chain update flow for tcp listener update which doesn't close connections if the corresponding network filter chain is equivalent during the listener update.
  Can be disabled by setting runtime feature `envoy.reloadable_features.listener_in_place_filterchain_update` to false.
  Also added additional draining filter chain stat for :ref:`listener manager <config_listener_manager_stats>` to track the number of draining filter chains and the number of in place update attempts.
* logger: added :ref:`--log-format-prefix-with-location <operations_cli>` command line option to prefix '%v' with file path and line number.
* lrs: added new *envoy_api_field_service.load_stats.v2.LoadStatsResponse.send_all_clusters* field
  in LRS response, which allows management servers to avoid explicitly listing all clusters it is
  interested in; behavior is allowed based on new "envoy.lrs.supports_send_all_clusters" capability
  in :ref:`client_features<envoy_v3_api_field_config.core.v3.Node.client_features>` field.
* network filters: added a :ref:`postgres proxy filter <config_network_filters_postgres_proxy>`.
* network filters: added a :ref:`rocketmq proxy filter <config_network_filters_rocketmq_proxy>`.
* prometheus stats: fix the sort order of output lines to comply with the standard.
* request_id: added to :ref:`always_set_request_id_in_response setting <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.always_set_request_id_in_response>`
  to set :ref:`x-request-id <config_http_conn_man_headers_x-request-id>` header in response even if
  tracing is not forced.
* router: allow retries of streaming or incomplete requests. This removes stat `rq_retry_skipped_request_not_complete`.
* router: allow retries by default when upstream responds with :ref:`x-envoy-overloaded <config_http_filters_router_x-envoy-overloaded_set>`.
* stats: added the option to :ref:`report counters as deltas <envoy_v3_api_field_config.metrics.v3.MetricsServiceConfig.report_counters_as_deltas>` to the metrics service stats sink.
* tracing: tracing configuration has been made fully dynamic and every HTTP connection manager
  can now have a separate :ref:`tracing provider <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing.provider>`.
* upstream: fixed a bug where Envoy would panic when receiving a GRPC SERVICE_UNKNOWN status on the health check.

Deprecated
----------

* Tracing provider configuration as part of :ref:`bootstrap config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.tracing>`
  has been deprecated in favor of configuration as part of :ref:`HTTP connection manager
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing.provider>`.
