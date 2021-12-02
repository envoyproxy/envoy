1.22.0 (pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*


Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* bandwidth_limit: added :ref:`response trailers <envoy_v3_api_field_extensions.filters.http.bandwidth_limit.v3.BandwidthLimit.enable_response_trailers>` when request or response delay are enforced.
* bandwidth_limit: added :ref:`bandwidth limit stats <config_http_filters_bandwidth_limit>` *request_enforced* and *response_enforced*.
* dns: now respecting the returned DNS TTL for resolved hosts, rather than always relying on the hard-coded :ref:`dns_refresh_rate. <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` This behavior can be temporarily reverted by setting the runtime guard ``envoy.reloadable_features.use_dns_ttl`` to false.
* http: directly response with http status code 1xx isn't valid usecase, so the status code 1xx was refused by the :ref:`direct_response <envoy_v3_api_field_config.route.v3.Route.direct_response>` field.
* http: envoy will now proxy 102 and 103 headers from upstream, though as with 100s only the first 1xx response headers will be sent. This behavioral change by can temporarily reverted by setting runtime guard ``envoy.reloadable_features.proxy_102_103`` to false.
* http: now the max concurrent streams of http2 connection can not only be adjusted down according to the SETTINGS frame but also can be adjusted up, of course, it can not exceed the configured upper bounds. This fix is guarded by ``envoy.reloadable_features.http2_allow_capacity_increase_by_settings``.
* http: usage of the experimental matching API is no longer guarded behind a feature flag, as the corresponding protobuf fields have been marked as WIP.
* http: when envoy run out of ``max_requests_per_connection``, it will send an HTTP/2 "shutdown nofitication" (GOAWAY frame with max stream ID) and go to a default grace period of 5000 milliseconds (5 seconds) if ``drain_timeout`` is not specified. During this grace period, envoy will continue to accept new streams. After the grace period, a final GOAWAY is sent and envoy will start refusing new streams. However before bugfix, during the grace period, every time a new stream is received, old envoy will always send a "shutdown notification" and restart drain again which actually causes the grace period to be extended and is no longer equal to ``drain_timeout``.
* json: switching from rapidjson to nlohmann/json. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.remove_legacy_json`` to false.
* listener: destroy per network filter chain stats when a network filter chain is removed during the listener in place update.
* quic: add back the support for IETF draft 29 which is guarded via ``envoy.reloadable_features.FLAGS_quic_reloadable_flag_quic_disable_version_draft_29``. It is off by default so Envoy only supports RFCv1 without flipping this runtime guard explicitly. Draft 29 is not recommended for use.
* router: take elapsed time into account when setting the x-envoy-expected-rq-timeout-ms header for retries, and never send a value that's longer than the request timeout. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.update_expected_rq_timeout_on_retry`` to false.
* stream_info: response code details with empty space characters (' ', '\t', '\f', '\v', '\n', '\r') is not accepted by the ``setResponseCodeDetails()`` API.
* upstream: fixed a bug where auto_config didn't work for wrapped TLS sockets (e.g. if proxy proto were configured for TLS).

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
