1.21.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* config: configuration files ending in .yml now load as YAML.
* config: configuration file extensions now ignore case when deciding the file type. E.g., .JSON file load as JSON.
* config: reduced log level for "Unable to establish new stream" xDS logs to debug. The log level
  for "gRPC config stream closed" is now reduced to debug when the status is ``Ok`` or has been
  retriable (``DeadlineExceeded``, ``ResourceExhausted``, or ``Unavailable``) for less than 30
  seconds.
* dns: now respecting the returned DNS TTL for resolved  hosts, rather than always relying on the hard-coded ref:`dns_refresh_rate. <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` This behavior can be temporarily reverted by setting the runtime guard ``envoy.reloadable_features.use_dns_ttl`` to false.
* grpc: gRPC async client can be cached and shared across filter instances in the same thread, this feature is turned off by default, can be turned on by setting runtime guard ``envoy.reloadable_features.enable_grpc_async_client_cache`` to true.
* http: correct the use of the ``x-forwarded-proto`` header and the ``:scheme`` header. Where they differ
  (which is rare) ``:scheme`` will now be used for serving redirect URIs and cached content. This behavior
  can be reverted by setting runtime guard ``correct_scheme_and_xfp`` to false.
* http: reject requests with #fragment in the URI path. The fragment is not allowed to be part of the request
  URI according to RFC3986 (3.5), RFC7230 (5.1) and RFC 7540 (8.1.2.3). Rejection of requests can be changed
  to stripping the #fragment instead by setting the runtime guard ``envoy.reloadable_features.http_reject_path_with_fragment``
  to false. This behavior can further be changed to the deprecated behavior of keeping the fragment by setting the runtime guard
  ``envoy.reloadable_features.http_strip_fragment_from_path_unsafe_if_disabled``. This runtime guard must only be set
  to false when existing non-compliant traffic relies on #fragment in URI. When this option is enabled, Envoy request
  authorization extensions may be bypassed. This override and its associated behavior will be decommissioned after the standard deprecation period.
* http: set the default :ref:`lazy headermap threshold <arch_overview_http_header_map_settings>` to 3,
  which defines the minimal number of headers in a request/response/trailers required for using a
  dictionary in addition to the list. Setting the ``envoy.http.headermap.lazy_map_min_size`` runtime
  feature to a non-negative number will override the default value.
* http: stop processing pending H/2 frames if connection transitioned to a closed state. This behavior can be temporarily reverted by setting the ``envoy.reloadable_features.skip_dispatching_frames_for_closed_connection`` to false.
* listener: added the :ref:`enable_reuse_port <envoy_v3_api_field_config.listener.v3.Listener.enable_reuse_port>`
  field and changed the default for reuse_port from false to true, as the feature is now well
  supported on the majority of production Linux kernels in use. The default change is aware of the hot
  restart, as otherwise, the change would not be backward compatible between restarts. This means
  that hot restarting onto a new binary will retain the default of false until the binary undergoes
  a full restart. To retain the previous behavior, either explicitly set the new configuration
  field to false, or set the runtime feature flag ``envoy.reloadable_features.listener_reuse_port_default_enabled``
  to false. As part of this change, the use of reuse_port for TCP listeners on both macOS and
  Windows has been disabled due to suboptimal behavior. See the field documentation for more
  information.
* listener: destroy per network filter chain stats when a network filter chain is removed during the listener in-place update.
* quic: enables IETF connection migration. This feature requires a stable UDP packet routine in the L4 load balancer with the same first-4-bytes in connection id. It can be turned off by setting runtime guard ``envoy.reloadable_features.FLAGS_quic_reloadable_flag_quic_connection_migration_use_new_cid_v2`` to false.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed ``envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure`` and legacy code paths.

New Features
------------

Deprecated
----------
