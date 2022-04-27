1.23.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* tls-inspector: the listener filter tls inspector's stats ``connection_closed`` and ``read_error`` are removed. The new stats are introduced for listener, ``downstream_peek_remote_close`` and ``read_error`` :ref:`listener stats <config_listener_stats>`.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* thrift: add validate_clusters in :ref:`RouteConfiguration <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.v3.RouteConfiguration>` to override the default behavior of cluster validation.
* tls: if both :ref:`match_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_subject_alt_names>` and :ref:`match_typed_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_typed_subject_alt_names>` are specified, the former (deprecated) field is ignored. Previously, setting both fields would result in an error.
* tls: removed SHA-1 cipher suites from the server-side defaults.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`
* compressor: removed ``envoy.reloadable_features.fix_added_trailers`` and legacy code paths.

* ext_authz: removed ``envoy.reloadable_features.http_ext_authz_do_not_skip_direct_response_and_redirect`` runtime guard and legacy code paths.
* http: deprecated ``envoy.reloadable_features.correct_scheme_and_xfp`` and legacy code paths.

New Features
------------
* access_log: added new access_log command operators to retrieve upstream connection information: ``%UPSTREAM_PROTOCOL%``, ``%UPSTREAM_PEER_SUBJECT%``, ``%UPSTREAM_PEER_ISSUER%``, ``%UPSTREAM_TLS_SESSION_ID%``, ``%UPSTREAM_TLS_CIPHER%``, ``%UPSTREAM_TLS_VERSION%``, ``%UPSTREAM_PEER_CERT_V_START%``, ``%UPSTREAM_PEER_CERT_V_END%`` and ``%UPSTREAM_PEER_CERT%``.
* dns_resolver: added :ref:`include_unroutable_families<envoy_v3_api_field_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig.include_unroutable_families>` to the Apple DNS resolver.
* ext_proc: added support for per-route :ref:`grpc_service <envoy_v3_api_field_extensions.filters.http.ext_proc.v3.ExtProcOverrides.grpc_service>`.

Deprecated
----------
