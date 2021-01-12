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
* expr filter: added `connection.termination_details` property support.
* ext_authz filter: disable `envoy.reloadable_features.ext_authz_measure_timeout_on_check_created` by default.
* ext_authz filter: the deprecated field :ref:`use_alpha <envoy_api_field_config.filter.http.ext_authz.v2.ExtAuthz.use_alpha>` is no longer supported and cannot be set anymore.
* grpc_web filter: if a `grpc-accept-encoding` header is present it's passed as-is to the upstream and if it isn't `grpc-accept-encoding:identity` is sent instead. The header was always overwriten with `grpc-accept-encoding:identity,deflate,gzip` before.
* http: upstream protocol will now only be logged if an upstream stream was established.
* jwt_authn filter: added support of Jwt time constraint verification with a clock skew (default to 60 seconds) and added a filter config field :ref:`clock_skew_seconds <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.clock_skew_seconds>` to configure it.
* kill_request: enable a way to configure kill header name in KillRequest proto.
* memory: enable new tcmalloc with restartable sequences for aarch64 builds.
* mongo proxy metrics: swapped network connection remote and local closed counters previously set reversed (`cx_destroy_local_with_active_rq` and `cx_destroy_remote_with_active_rq`).
* oauth filter: added the optional parameter :ref:`auth_scopes <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.auth_scopes>` with default value of 'user' if not provided. Enables this value to be overridden in the Authorization request to the OAuth provider.
* performance: improve performance when handling large HTTP/1 bodies.
* tls: removed RSA key transport and SHA-1 cipher suites from the client-side defaults.
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
* tcp_proxy: add support for converting raw TCP streams into HTTP/1.1 CONNECT requests. See :ref:`upgrade documentation <tunneling-tcp-over-http>` for details.

Deprecated
----------
