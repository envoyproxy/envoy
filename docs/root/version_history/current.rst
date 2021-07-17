1.20.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* grpc: gRPC async client can be cached and shared accross filter instances in the same thread, this feature is turned off by default, can be turned on by setting runtime guard ``envoy.reloadable_features.enable_grpc_async_client_cache`` to true.
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
* listener: removed ``envoy.reloadable_features.disable_tls_inspector_injection`` runtime guard and legacy code paths.

New Features
------------
* http: added :ref:`string_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.string_match>` in the header matcher.

Deprecated
----------
* http: the HeaderMatcher fields :ref:`exact_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.exact_match>`, :ref:`safe_regex_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.safe_regex_match>`,
  :ref:`prefix_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.prefix_match>`, :ref:`suffix_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.suffix_match>` and
  :ref:`contains_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.contains_match>` are deprecated by :ref:`string_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.string_match>`.
