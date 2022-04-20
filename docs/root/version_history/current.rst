1.23.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* tls-inspector: the listener filter tls inspector's stats ``connection_closed`` and ``read_error`` are removed. The new stats are introduced for listener, ``downstream_peek_remote_close`` and ``read_error`` :ref:`listener stats <config_listener_stats>`.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* tls: removed SHA-1 cipher suites from the server-side defaults.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------
* ext_proc: added support for per-route :ref:`grpc_service <envoy_v3_api_field_extensions.filters.http.ext_proc.v3.ExtProcOverrides.grpc_service>`.
* http3: add :ref:`early_data_option <envoy_v3_api_field_config.route.v3.RouteAction.early_data_option>` extension to allow upstream HTTP/3 sending requests over early data. If no extension is configured or :ref:`early_data_allows_safe_requests <envoy_v3_api_field_extensions.early_data_option.v3.DefaultEarlyDataOption.early_data_allows_safe_requests>` is configured true, HTTP/3 pool will send safe requests as early data to the host if the pool already cached 0-RTT credentials of that host. If those requests fail and the underlying connection pool supports TCP fallback, the request may be retried automatically. If :ref:`early_data_allows_safe_requests <envoy_v3_api_field_extensions.early_data_option.v3.DefaultEarlyDataOption.early_data_allows_safe_requests>` is configured false, no requests are allowed to be sent as early data. Note that if any customized extension configures non-safe requests to be allowed over early data, the Envoy will not automatically retry them. If desired, explicitly config their :ref:`retry_policy <envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>`. This feature requires both ``envoy.reloadable_features.conn_pool_new_stream_with_early_data_and_http3`` and ``"envoy.reloadable_features.http3_sends_early_data`` to be turned on.

Deprecated
----------
