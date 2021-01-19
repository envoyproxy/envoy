1.18.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* tcp: setting NODELAY in the base connection class. This should have no effect for TCP or HTTP proxying, but may improve throughput in other areas. This behavior can be temporarily reverted by setting `envoy.reloadable_features.always_nodelay` to false.
* upstream: host weight changes now cause a full load balancer rebuild as opposed to happening
  atomically inline. This change has been made to support load balancer pre-computation of data
  structures based on host weight, but may have performance implications if host weight changes
  are very frequent. This change can be disabled by setting the `envoy.reloadable_features.upstream_host_weight_change_causes_rebuild`
  feature flag to false. If setting this flag to false is required in a deployment please open an
  issue against the project.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* active http health checks: properly handles HTTP/2 GOAWAY frames from the upstream. Previously a GOAWAY frame due to a graceful listener drain could cause improper failed health checks due to streams being refused by the upstream on a connection that is going away. To revert to old GOAWAY handling behavior, set the runtime feature `envoy.reloadable_features.health_check.graceful_goaway_handling` to false.
* buffer: tighten network connection read and write buffer high watermarks in preparation to more careful enforcement of read limits. Buffer high-watermark is now set to the exact configured value; previously it was set to value + 1.
* http: reverting a behavioral change where upstream connect timeouts were temporarily treated differently from other connection failures. The change back to the original behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure` to false.
* upstream: fix handling of moving endpoints between priorities when active health checks are enabled. Previously moving to a higher numbered priority was a NOOP, and moving to a lower numbered priority caused an abort.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* access_logs: removed legacy unbounded access logs and runtime guard `envoy.reloadable_features.disallow_unbounded_access_logs`.
* dynamic_forward_proxy: removed `envoy.reloadable_features.enable_dns_cache_circuit_breakers` and legacy code path.
* http: removed legacy connection close behavior and runtime guard `envoy.reloadable_features.fixed_connection_close`.
* http: removed legacy HTTP/1.1 error reporting path and runtime guard `envoy.reloadable_features.early_errors_via_hcm`.

New Features
------------
* access log: added the :ref:`formatters <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.formatters>` extension point for custom formatters (command operators).
* access log: support command operator: %REQUEST_HEADERS_BYTES%, %RESPONSE_HEADERS_BYTES% and %RESPONSE_TRAILERS_BYTES%.
* http: added support for :ref:`:ref:`preconnecting <envoy_v3_api_msg_config.cluster.v3.Cluster.PreconnectPolicy>`. Preconnecting is off by default, but recommended for clusters serving latency-sensitive traffic, especially if using HTTP/1.1.
* http: change frame flood and abuse checks to the upstream HTTP/2 codec to ON by default. It can be disabled by setting the `envoy.reloadable_features.upstream_http2_flood_checks` runtime key to false.
* tcp_proxy: add support for converting raw TCP streams into HTTP/1.1 CONNECT requests. See :ref:`upgrade documentation <tunneling-tcp-over-http>` for details.

Deprecated
----------
