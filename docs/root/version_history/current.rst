1.16.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* compressor: always insert `Vary` headers for compressible resources even if it's decided not to compress a response due to incompatible `Accept-Encoding` value. The `Vary` header needs to be inserted to let a caching proxy in front of Envoy know that the requested resource still can be served with compression applied.
* http: the per-stream FilterState maintained by the HTTP connection manager will now provide read/write access to the downstream connection FilterState. As such, code that relies on interacting with this might
  see a change in behavior.
* logging: nghttp2 log messages no longer appear at trace level unless `ENVOY_NGHTTP2_TRACE` is set
  in the environment.
* router: now consumes all retry related headers to prevent them from being propagated to the upstream. This behavior may be reverted by setting runtime feature `envoy.reloadable_features.consume_all_retry_headers` to false.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* fault: fixed an issue with `active_faults` gauge not being decremented for when abort faults were injected.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed legacy header sanitization and the runtime guard `envoy.reloadable_features.strict_header_validation`.
* http: removed legacy transfer-encoding enforcement and runtime guard `envoy.reloadable_features.reject_unsupported_transfer_encodings`.
* http: removed configurable strict host validation and runtime guard `envoy.reloadable_features.strict_authority_validation`.

New Features
------------

* ext_authz filter: added support for emitting dynamic metadata for both :ref:`HTTP <config_http_filters_ext_authz_dynamic_metadata>` and :ref:`network <config_network_filters_ext_authz_dynamic_metadata>` filters.
* grpc-json: support specifying `response_body` field in for `google.api.HttpBody` message.
* load balancer: added a :ref:`configuration<envoy_v3_api_msg_config.cluster.v3.Cluster.LeastRequestLbConfig>` option to specify the active request bias used by the least request load balancer.
* tap: added :ref:`generic body matcher<envoy_v3_api_msg_config.tap.v3.HttpGenericBodyMatch>` to scan http requests and responses for text or hex patterns.

Deprecated
----------
