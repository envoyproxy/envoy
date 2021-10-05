1.21.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* access log: fix ``%UPSTREAM_CLUSTER%`` when used in http upstream access logs. Previously, it was always logging as an unset value.
* aws request signer: fix the AWS Request Signer extension to correctly normalize the path and query string to be signed according to AWS' guidelines, so that the hash on the server side matches. See `AWS SigV4 documentation <https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html>`_.
* cluster: delete pools when they're idle to fix unbounded memory use when using PROXY protocol upstream with tcp_proxy. This behavior can be temporarily reverted by setting the ``envoy.reloadable_features.conn_pool_delete_when_idle`` runtime guard to false.
* cluster: finish cluster warming even if hosts are removed before health check initialization. This only affected clusters with :ref:`ignore_health_on_host_removal <envoy_v3_api_field_config.cluster.v3.Cluster.ignore_health_on_host_removal>`.
* compressor: fix a bug where if trailers were added and a subsequent filter paused the filter chain, the request could be stalled. This behavior can be reverted by setting ``envoy.reloadable_features.fix_added_trailers`` to false.
* dynamic forward proxy: fixing a validation bug where san and sni checks were not applied setting :ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` via :ref:`typed_extension_protocol_options <envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>`.
* ext_authz: fix the ext_authz filter to correctly merge multiple same headers using the ',' as separator in the check request to the external authorization service.
* ext_authz: fix the use of ``append`` field of :ref:`response_headers_to_add <envoy_v3_api_field_service.auth.v3.OkHttpResponse.response_headers_to_add>` to set or append encoded response headers from a gRPC auth server.
* ext_authz: fix the HTTP ext_authz filter to respond with ``403 Forbidden`` when a gRPC auth server sends a denied check response with an empty HTTP status code.
* ext_authz: the network ext_authz filter now correctly sets dynamic metadata returned by the authorization service for non-OK responses. This behavior now matches the http ext_authz filter.
* hcm: remove deprecation for :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` and forbid mixing ip detection extensions with old related knobs.
* http: limit use of deferred resets in the http2 codec to server-side connections. Use of deferred reset for client connections can result in incorrect behavior and performance problems.
* listener: fixed an issue on Windows where connections are not handled by all worker threads.
* lua: fix ``BodyBuffer`` setting a Lua string and printing Lua string containing hex characters. Previously, ``BodyBuffer`` setting a Lua string or printing strings with hex characters will be truncated.
* thrift_proxy: fix the thrift_proxy connection manager to correctly report success/error response metrics when performing :ref:`payload passthrough <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.payload_passthrough>`.
* xray: fix the AWS X-Ray tracer bug where span's error, fault and throttle information was not reported properly as per the `AWS X-Ray documentation <https://docs.aws.amazon.com/xray/latest/devguide/xray-api-segmentdocuments.html>`_. Before this fix, server error was reported under the 'annotations' section of the segment data.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
