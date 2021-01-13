1.18.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* upstream: host weight changes now cause a full load balancer rebuild as opposed to happening
  atomically inline. This change has been made to support load balancer pre-computation of data
  structures based on host weight, but may have performance implications if host weight changes
  are very frequent. This change can be disabled by setting the `envoy.reloadable_features.upstream_host_weight_change_causes_rebuild`
  feature flag to false. If setting this flag to false is required in a deployment please open an
  issue against the project.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* config: validate that upgrade configs have a non-empty :ref:`upgrade_type <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig.upgrade_type>`, fixing a bug where an errant "-" could result in unexpected behavior.
* dns: fixed a bug where custom resolvers provided in configuration were not preserved after network issues.
* dns_filter: correctly associate DNS response IDs when multiple queries are received.
* grpc mux: fix sending node again after stream is reset when ::ref:`set_node_on_first_message_only <envoy_api_field_core.ApiConfigSource.set_node_on_first_message_only>` is set.
* grpc-web: fix local reply and non-proto-encoded gRPC response handling for small response bodies. This fix can be temporarily reverted by setting `envoy.reloadable_features.grpc_web_fix_non_grpc_response_handling` to false.
* grpc mux: fixed sending node again after stream is reset when :ref:`set_node_on_first_message_only <envoy_api_field_core.ApiConfigSource.set_node_on_first_message_only>` is set.
* http: fixed URL parsing for HTTP/1.1 fully qualified URLs and connect requests containing IPv6 addresses.
* http: reject requests with missing required headers after filter chain processing.
* http: sending CONNECT_ERROR for HTTP/2 where appropriate during CONNECT requests.
* proxy_proto: fixed a bug where the wrong downstream address got sent to upstream connections.
* proxy_proto: fixed a bug where network filters would not have the correct downstreamRemoteAddress() when accessed from the StreamInfo. This could result in incorrect enforcement of RBAC rules in the RBAC network filter (but not in the RBAC HTTP filter), or incorrect access log addresses from tcp_proxy.
* sds: fixed a bug that clusters sharing same sds target are marked active immediately.
* tls: fixed detection of the upstream connection close event.
* tls: fixed read resumption after triggering buffer high-watermark and all remaining request/response bytes are stored in the SSL connection's internal buffers.
* udp: fixed issue in which receiving truncated UDP datagrams would cause Envoy to crash.
* watchdog: touch the watchdog before most event loop operations to avoid misses when handling bursts of callbacks.
* active http health checks: properly handles HTTP/2 GOAWAY frames from the upstream. Previously a GOAWAY frame due to a graceful listener drain could cause improper failed health checks due to streams being refused by the upstream on a connection that is going away. To revert to old GOAWAY handling behavior, set the runtime feature `envoy.reloadable_features.health_check.graceful_goaway_handling` to false.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* access_logs: removed legacy unbounded access logs and runtime guard `envoy.reloadable_features.disallow_unbounded_access_logs`.

New Features
------------
* access log: added the :ref:`formatters <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.formatters>` extension point for custom formatters (command operators).
* tcp_proxy: add support for converting raw TCP streams into HTTP/1.1 CONNECT requests. See :ref:`upgrade documentation <tunneling-tcp-over-http>` for details.

Deprecated
----------
