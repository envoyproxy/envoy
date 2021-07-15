1.20.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* grpc: gRPC async client can be cached and shared accross filter instances in the same thread, this feature is turned off by default, can be turned on by setting runtime guard ``envoy.reloadable_features.enable_grpc_async_client_cache`` to true.* tracing: add option :ref:`use_request_id_for_trace_sampling <envoy_v3_api_field_extensions.request_id.uuid.v3.UuidRequestIdConfig.use_request_id_for_trace_sampling>` whether to use sampling policy based on :ref:`x-request-id<config_http_conn_man_headers_x-request-id>` or not.
* http: set the default :ref:`lazy headermap threshold <arch_overview_http_header_map_settings>` to 3,
  which defines the minimal number of headers in a request/response/trailers required for using a
  dictionary in addition to the list. Setting the `envoy.http.headermap.lazy_map_min_size` runtime
  feature to a non-negative number will override the default value.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed ``envoy.reloadable_features.http_upstream_wait_connect_response`` runtime guard and legacy code paths.
* http: removed ``envoy.reloadable_features.allow_preconnect`` runtime guard and legacy code paths.

New Features
------------

Deprecated
----------
