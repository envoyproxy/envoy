1.16.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* access loggers: applied existing buffer limits to access logs, as well as :ref:`stats <config_access_log_stats>` for logged / dropped logs. This can be reverted temporarily by setting runtime feature `envoy.reloadable_features.disallow_unbounded_access_logs` to false.
* build: run as non-root inside Docker containers. Existing behaviour can be restored by setting the environment variable `ENVOY_UID` to `0`. `ENVOY_UID` and `ENVOY_GID` can be used to set the envoy user's `uid` and `gid` respectively.
* header to metadata: on_header_missing rules with empty values are now rejected (they were skipped before).
* health check: in the health check filter the :ref:`percentage of healthy servers in upstream clusters <envoy_api_field_config.filter.http.health_check.v2.HealthCheck.cluster_min_healthy_percentages>` is now interpreted as an integer.
* hot restart: added the option :option:`--use-dynamic-base-id` to select an unused base ID at startup and the option :option:`--base-id-path` to write the base id to a file (for reuse with later hot restarts).
* http: changed early error path for HTTP/1.1 so that responses consistently flow through the http connection manager, and the http filter chains. This behavior may be temporarily reverted by setting runtime feature `envoy.reloadable_features.early_errors_via_hcm` to false.
* http: fixed several bugs with applying correct connection close behavior across the http connection manager, health checker, and connection pool. This behavior may be temporarily reverted by setting runtime feature `envoy.reloadable_features.fix_connection_close` to false.
* http: fixed a bug where the upgrade header was not cleared on responses to non-upgrade requests.
  Can be reverted temporarily by setting runtime feature `envoy.reloadable_features.fix_upgrade_response` to false.
* http: stopped overwriting `date` response headers. Responses without a `date` header will still have the header properly set. This behavior can be temporarily reverted by setting `envoy.reloadable_features.preserve_upstream_date` to false.
* http: stopped adding a synthetic path to CONNECT requests, meaning unconfigured CONNECT requests will now return 404 instead of 403. This behavior can be temporarily reverted by setting `envoy.reloadable_features.stop_faking_paths` to false.
* http: stopped allowing upstream 1xx or 204 responses with Transfer-Encoding or non-zero Content-Length headers. Content-Length of 0 is allowed, but stripped. This behavior can be temporarily reverted by setting `envoy.reloadable_features.strict_1xx_and_204_response_headers` to false.
* http: the per-stream FilterState maintained by the HTTP connection manager will now provide read/write access to the downstream connection FilterState. As such, code that relies on interacting with this might
  see a change in behavior.
* http: upstream connections will now automatically set ALPN when this value is not explicitly set elsewhere (e.g. on the upstream TLS config). This behavior may be temporarily reverted by setting runtime feature `envoy.reloadable_features.http_default_alpn` to false.
* listener: fixed a bug where when a static listener fails to be added to a worker, the listener was not removed from the active listener list.
* router: allow retries of streaming or incomplete requests. This removes stat `rq_retry_skipped_request_not_complete`.
* router: allow retries by default when upstream responds with :ref:`x-envoy-overloaded <config_http_filters_router_x-envoy-overloaded_set>`.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed legacy header sanitization and the runtime guard `envoy.reloadable_features.strict_header_validation`.

New Features
------------

Deprecated
----------

