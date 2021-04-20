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

* zipkin: fix timestamp serializaiton in annotations. A prior bug fix exposed an issue with timestamps being serialized as strings.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed `envoy.reloadable_features.overload_manager_disable_keepalive_drain_http2`; Envoy will now always send GOAWAY to HTTP2 downstreams when the :ref:`disable_keepalive <config_overload_manager_overload_actions>` overload action is active.
* tls: removed `envoy.reloadable_features.tls_use_io_handle_bio` runtime guard and legacy code path.

New Features
------------

* http: added support for :ref:`original IP detection extensions<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
  Two initial extensions were added, the :ref:`custom header <envoy_v3_api_msg_extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig>` extension and the
  :ref:`xff <envoy_v3_api_msg_extensions.http.original_ip_detection.xff.v3.XffConfig>` extension.

Deprecated
----------

* http: :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` is deprecated in favor of :ref:`original IP detection extensions<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
