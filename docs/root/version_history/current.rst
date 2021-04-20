1.19.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* http: replaced setting `envoy.reloadable_features.strict_1xx_and_204_response_headers` with settings
  `envoy.reloadable_features.require_strict_1xx_and_204_response_headers`
  (require upstream 1xx or 204 responses to not have Transfer-Encoding or non-zero Content-Length headers) and
  `envoy.reloadable_features.send_strict_1xx_and_204_response_headers`
  (do not send 1xx or 204 responses with these headers). Both are true by default.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* access_logs: removed legacy unbounded access logs and runtime guard `envoy.reloadable_features.disallow_unbounded_access_logs`.
* dns: removed legacy buggy wildcard matching path and runtime guard `envoy.reloadable_features.fix_wildcard_matching`.
* dynamic_forward_proxy: removed `envoy.reloadable_features.enable_dns_cache_circuit_breakers` and legacy code path.
* gzip: removed legacy HTTP Gzip filter and runtime guard `envoy.deprecated_features.allow_deprecated_gzip_http_filter`.
* http: removed legacy connect behavior and runtime guard `envoy.reloadable_features.stop_faking_paths`.
* http: removed legacy connection close behavior and runtime guard `envoy.reloadable_features.fixed_connection_close`.
* http: removed legacy HTTP/1.1 error reporting path and runtime guard `envoy.reloadable_features.early_errors_via_hcm`.
* http: removed legacy sanitization path for upgrade response headers and runtime guard `envoy.reloadable_features.fix_upgrade_response`.
* http: removed legacy date header overwriting logic and runtime guard `envoy.reloadable_features.preserve_upstream_date deprecation`.
* http: removed legacy ALPN handling and runtime guard `envoy.reloadable_features.http_default_alpn`.
* listener: removed legacy runtime guard `envoy.reloadable_features.listener_in_place_filterchain_update`.
* router: removed `envoy.reloadable_features.consume_all_retry_headers` and legacy code path.
* router: removed `envoy.reloadable_features.preserve_query_string_in_path_redirects` and legacy code path.
* tls: removed `envoy.reloadable_features.tls_use_io_handle_bio` runtime guard and legacy code path.

New Features
------------

Deprecated
----------
