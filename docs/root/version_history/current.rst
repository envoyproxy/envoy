1.18.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* build: the Alpine based debug images are no longer built in CI, use Ubuntu based images instead.
* cluster manager: the cluster which can't extract secret entity by SDS to be warming and never activate. This feature is disabled by default and is controlled by runtime guard `envoy.reloadable_features.cluster_keep_warming_no_secret_entity`.
* decompressor: set the default value of window_bits of the decompressor to 15 to be able to decompress responses compressed by a compressor with any window size.
* expr filter: added `connection.termination_details` property support.
* ext_authz filter: disable `envoy.reloadable_features.ext_authz_measure_timeout_on_check_created` by default.
* ext_authz filter: the deprecated field :ref:`use_alpha <envoy_api_field_config.filter.http.ext_authz.v2.ExtAuthz.use_alpha>` is no longer supported and cannot be set anymore.
* formatter: the :ref:`text_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format>` field no longer requires at least one byte, and may now be the empty string. It has also become deprecated: see Deprecated section.
* grpc_web filter: if a `grpc-accept-encoding` header is present it's passed as-is to the upstream and if it isn't `grpc-accept-encoding:identity` is sent instead. The header was always overwriten with `grpc-accept-encoding:identity,deflate,gzip` before.
* http: upstream protocol will now only be logged if an upstream stream was established.
* jwt_authn filter: added support of Jwt time constraint verification with a clock skew (default to 60 seconds) and added a filter config field :ref:`clock_skew_seconds <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.clock_skew_seconds>` to configure it.
* kill_request: enable a way to configure kill header name in KillRequest proto.
* listener: injection of the :ref:`TLS inspector <config_listener_filters_tls_inspector>` has been disabled by default. This feature is controlled by the runtime guard `envoy.reloadable_features.disable_tls_inspector_injection`.
* memory: enable new tcmalloc with restartable sequences for aarch64 builds.
* mongo proxy metrics: swapped network connection remote and local closed counters previously set reversed (`cx_destroy_local_with_active_rq` and `cx_destroy_remote_with_active_rq`).
* outlier detection: added :ref:`max_ejection_time <envoy_v3_api_field_config.cluster.v3.OutlierDetection.max_ejection_time>` to limit ejection time growth when a node stays unhealthy for extended period of time. By default :ref:`max_ejection_time <envoy_v3_api_field_config.cluster.v3.OutlierDetection.max_ejection_time>` limits ejection time to 5 minutes. Additionally, when the node stays healthy, ejection time decreases. See :ref:`ejection algorithm<arch_overview_outlier_detection_algorithm>` for more info. Previously, ejection time could grow without limit and never decreased.
* performance: improve performance when handling large HTTP/1 bodies.
* tls: removed RSA key transport and SHA-1 cipher suites from the client-side defaults.
* upstream: host weight changes now cause a full load balancer rebuild as opposed to happening
  atomically inline. This change has been made to support load balancer pre-computation of data
  structures based on host weight, but may have performance implications if host weight changes
  are very frequent. This change can be disabled by setting the `envoy.reloadable_features.upstream_host_weight_change_causes_rebuild`
  feature flag to false. If setting this flag to false is required in a deployment please open an
  issue against the project.
* watchdog: the watchdog action :ref:`abort_action <envoy_v3_api_msg_watchdog.v3alpha.AbortActionConfig>` is now the default action to terminate the process if watchdog kill / multikill is enabled.
* xds: to support TTLs, heartbeating has been added to xDS. As a result, responses that contain empty resources without updating the version will no longer be propagated to the
  subscribers. To undo this for VHDS (which is the only subscriber that wants empty resources), the `envoy.reloadable_features.vhds_heartbeats` can be set to "false".

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------