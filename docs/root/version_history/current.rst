1.15.3 (Pending)
================

Changes
-------
* listener: fix crash when disabling or re-enabling listeners due to overload while processing LDS updates.
* proxy_proto: fixed a bug where network filters would not have the correct downstreamRemoteAddress() when accessed from the StreamInfo. This could result in incorrect enforcement of RBAC rules in the RBAC network filter (but not in the RBAC HTTP filter), or incorrect access log addresses from tcp_proxy.
Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* build: the Alpine based debug images are no longer built in CI, use Ubuntu based images instead.
* cluster manager: the cluster which can't extract secret entity by SDS to be warming and never activate. This feature is disabled by default and is controlled by runtime guard `envoy.reloadable_features.cluster_keep_warming_no_secret_entity`.
* ext_authz filter: disable `envoy.reloadable_features.ext_authz_measure_timeout_on_check_created` by default.
* ext_authz filter: the deprecated field :ref:`use_alpha <envoy_api_field_config.filter.http.ext_authz.v2.ExtAuthz.use_alpha>` is no longer supported and cannot be set anymore.
* grpc_web filter: if a `grpc-accept-encoding` header is present it's passed as-is to the upstream and if it isn't `grpc-accept-encoding:identity` is sent instead. The header was always overwriten with `grpc-accept-encoding:identity,deflate,gzip` before.
* watchdog: the watchdog action :ref:`abort_action <envoy_v3_api_msg_watchdog.v3alpha.AbortActionConfig>` is now the default action to terminate the process if watchdog kill / multikill is enabled.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* http: fixed URL parsing for HTTP/1.1 fully qualified URLs and connect requests containing IPv6 addresses.
* http: sending CONNECT_ERROR for HTTP/2 where appropriate during CONNECT requests.
* tls: fix read resumption after triggering buffer high-watermark and all remaining request/response bytes are stored in the SSL connection's internal buffers.
* udp: fixed issue in which receiving truncated UDP datagrams would cause Envoy to crash.
