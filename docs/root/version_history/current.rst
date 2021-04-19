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

* active http health checks: properly handles HTTP/2 GOAWAY frames from the upstream. Previously a GOAWAY frame due to a graceful listener drain could cause improper failed health checks due to streams being refused by the upstream on a connection that is going away. To revert to old GOAWAY handling behavior, set the runtime feature `envoy.reloadable_features.health_check.graceful_goaway_handling` to false.
* adaptive concurrency: fixed a bug where concurrent requests on different worker threads could update minRTT back-to-back.
* buffer: tighten network connection read and write buffer high watermarks in preparation to more careful enforcement of read limits. Buffer high-watermark is now set to the exact configured value; previously it was set to value + 1.
* cdn_loop: check that the entirety of the :ref:`cdn_id <envoy_v3_api_field_extensions.filters.http.cdn_loop.v3alpha.CdnLoopConfig.cdn_id>` field is a valid CDN identifier.
* cds: fix blocking the update for a warming cluster when the update is the same as the active version.
* ext_authz: emit :ref:`CheckResponse.dynamic_metadata <envoy_v3_api_field_service.auth.v3.CheckResponse.dynamic_metadata>` when the external authorization response has "Denied" check status.
* fault injection: stop counting as active fault after delay elapsed. Previously fault injection filter continues to count the injected delay as an active fault even after it has elapsed. This produces incorrect output statistics and impacts the max number of consecutive faults allowed (e.g., for long-lived streams). This change decreases the active fault count when the delay fault is the only active and has gone finished.
* filter_chain: fix filter chain matching with the server name as the case-insensitive way.
* grpc-web: fix local reply and non-proto-encoded gRPC response handling for small response bodies. This fix can be temporarily reverted by setting `envoy.reloadable_features.grpc_web_fix_non_proto_encoded_response_handling` to false.
* grpc_http_bridge: the downstream HTTP status is now correctly set for trailers-only responses from the upstream.
* header map: pick the right delimiter to append multiple header values to the same key. Previouly header with multiple values are coalesced with ",", after this fix cookie headers should be coalesced with " ;". This doesn't affect Http1 or Http2 requests because these 2 codecs coalesce cookie headers before adding it to header map. To revert to the old behavior, set the runtime feature `envoy.reloadable_features.header_map_correctly_coalesce_cookies` to false.
* http: avoid grpc-status overwrite on Http::Utility::sendLocalReply() if that field has already been set.
* http: disallowing "host:" in request_headers_to_add for behavioral consistency with rejecting :authority header. This behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_host_like_authority` to false.
* http: fixed an issue where Enovy did not handle peer stream limits correctly, and queued streams in nghttp2 rather than establish new connections. This behavior can be temporarily reverted by setting `envoy.reloadable_features.improved_stream_limit_handling` to false.
* http: fixed a bug where setting :ref:`MaxStreamDuration proto <envoy_v3_api_msg_config.route.v3.RouteAction.MaxStreamDuration>` did not disable legacy timeout defaults.
* http: reverting a behavioral change where upstream connect timeouts were temporarily treated differently from other connection failures. The change back to the original behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure` to false.
* jwt_authn: reject requests with a proper error if JWT has the wrong issuer when allow_missing is used. Before this change, the requests are accepted.
* listener: prevent crashing when an unknown listener config proto is received and debug logging is enabled.
* mysql_filter: improve the codec ability of mysql filter at connection phase, it can now decode MySQL5.7+ connection phase protocol packet.
* overload: fix a bug that can cause use-after-free when one scaled timer disables another one with the same duration.
* sni: as the server name in sni should be case-insensitive, envoy will convert the server name as lower case first before any other process inside envoy.
* tls: fix the subject alternative name of the presented certificate matches the specified matchers as the case-insensitive way when it uses DNS name.
* tls: fix issue where OCSP was inadvertently removed from SSL response in multi-context scenarios.
* upstream: fix handling of moving endpoints between priorities when active health checks are enabled. Previously moving to a higher numbered priority was a NOOP, and moving to a lower numbered priority caused an abort.
* upstream: retry budgets will now set default values for xDS configurations.
* validation: fix an issue that causes TAP sockets to panic during config validation mode.
* zipkin: fix 'verbose' mode to emit annotations for stream events. This was the documented behavior, but wasn't behaving as documented.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
