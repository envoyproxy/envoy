1.20.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* access_log: add new access_log command operator ``%REQUEST_TX_DURATION%``.
* access_log: remove extra quotes on metadata string values. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.unquote_log_string_values`` to false.
* admission control: added :ref:`admission control <envoy_v3_api_field_extensions.filters.http.admission_control.v3alpha.AdmissionControl.max_rejection_probability>` whose default value is 80%, which means that the upper limit of the default rejection probability of the filter is changed from 100% to 80%.
* aws_request_signing: requests are now buffered by default to compute signatures which include the
  payload hash, making the filter compatible with most AWS services. Previously, requests were
  never buffered, which only produced correct signatures for requests without a body, or for
  requests to S3, ES or Glacier, which used the literal string ``UNSIGNED-PAYLOAD``. Buffering can
  be now be disabled in favor of using unsigned payloads with compatible services via the new
  ``use_unsigned_payload`` filter option (default false).
* cluster: added default value of 5 seconds for :ref:`connect_timeout <envoy_v3_api_field_config.cluster.v3.Cluster.connect_timeout>`.
* dns: changed apple resolver implementation to not reuse the UDS to the local DNS daemon.
* dns cache: the new :ref:`dns_query_timeout <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_query_timeout>` option has a default of 5s. See below for more information.
* http: disable the integration between :ref:`ExtensionWithMatcher <envoy_v3_api_msg_extensions.common.matching.v3.ExtensionWithMatcher>`
  and HTTP filters by default to reflects its experimental status. This feature can be enabled by seting
  ``envoy.reloadable_features.experimental_matching_api`` to true.
* http: replaced setting ``envoy.reloadable_features.strict_1xx_and_204_response_headers`` with settings
  ``envoy.reloadable_features.require_strict_1xx_and_204_response_headers``
  (require upstream 1xx or 204 responses to not have Transfer-Encoding or non-zero Content-Length headers) and
  ``envoy.reloadable_features.send_strict_1xx_and_204_response_headers``
  (do not send 1xx or 204 responses with these headers). Both are true by default.
* http: serve HEAD requests from cache.
* http: stop sending the transfer-encoding header for 304. This behavior can be temporarily reverted by setting
  ``envoy.reloadable_features.no_chunked_encoding_header_for_304`` to false.
* http: the behavior of the ``present_match`` in route header matcher changed. The value of ``present_match`` is ignored in the past. The new behavior is ``present_match`` performed when value is true. absent match performed when the value is false. Please reference :ref:`present_match
  <envoy_v3_api_field_config.route.v3.HeaderMatcher.present_match>`.
* listener: added an option when balancing across active listeners and wildcard matching is used to return the listener that matches the IP family type associated with the listener's socket address. Any unexpected behavioral changes can be reverted by setting runtime guard ``envoy.reloadable_features.listener_wildcard_match_ip_family`` to false.
* listener: added the :ref:`enable_reuse_port <envoy_v3_api_field_config.listener.v3.Listener.enable_reuse_port>`
  field and changed the default for reuse port from false to true, as the feature is now well
  supported on the majority of production Linux kernels in use. The default change is aware of hot
  restart, as otherwise the change would not be backwards compatible between restarts. This means
  that hot restarting on to a new binary will retain the default of false until the binary undergoes
  a full restart. To retain the previous behavior, either explicitly set the new configuration
  field to false, or set the runtime feature flag `envoy.reloadable_features.listener_reuse_port_default_enabled`
  to false. As part of this change, the use of reuse port for TCP listeners on both OSX and
  Windows has been disabled due to suboptimal behavior. See the field documentation for more
  information.
* listener: respect the :ref:`connection balance config <envoy_v3_api_field_config.listener.v3.Listener.connection_balance_config>`
  defined within the listener where the sockets are redirected to. Clear that field to restore the previous behavior.
* tcp: switched to the new connection pool by default. Any unexpected behavioral changes can be reverted by setting runtime guard ``envoy.reloadable_features.new_tcp_connection_pool`` to false.
* tracing: add option :ref:`use_request_id_for_trace_sampling <envoy_v3_api_field_extensions.request_id.uuid.v3.UuidRequestIdConfig.use_request_id_for_trace_sampling>` whether to use sampling policy based on :ref:`x-request-id<config_http_conn_man_headers_x-request-id>` or not.

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

* bootstrap: the field :ref:`use_tcp_for_dns_lookups <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.use_tcp_for_dns_lookups>` is deprecated in favor of :ref:`dns_resolution_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dns_resolution_config>` which aggregates all of the DNS resolver configuration in a single message.
* cluster: the fields :ref:`use_tcp_for_dns_lookups <envoy_v3_api_field_config.cluster.v3.Cluster.use_tcp_for_dns_lookups>` and :ref:`dns_resolvers <envoy_v3_api_field_config.cluster.v3.Cluster.dns_resolvers>` are deprecated in favor of :ref:`dns_resolution_config <envoy_v3_api_field_config.cluster.v3.Cluster.dns_resolution_config>` which aggregates all of the DNS resolver configuration in a single message.
* dns_filter: the field :ref:`known_suffixes <envoy_v3_api_field_data.dns.v3.DnsTable.known_suffixes>` is deprecated. The internal data management of the filter has changed and the filter no longer uses the known_suffixes field.
* dynamic_forward_proxy: the field :ref:`use_tcp_for_dns_lookups <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.use_tcp_for_dns_lookups>` is deprecated in favor of :ref:`dns_resolution_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_resolution_config>` which aggregates all of the DNS resolver configuration in a single message.
* http: :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` is deprecated in favor of :ref:`original IP detection extensions<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
* listener: :ref:`reuse_port <envoy_v3_api_field_config.listener.v3.Listener.reuse_port>` has been
  deprecated in favor of :ref:`enable_reuse_port <envoy_v3_api_field_config.listener.v3.Listener.enable_reuse_port>`.
  At the same time, the default has been changed from false to true. See above for more information.
