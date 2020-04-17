.. _config_http_filters_local_rate_limit:

Local rate limit
================

* Local rate limiting :ref:`architecture overview <arch_overview_local_rate_limit>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.local_rate_limit.v2.LocalRateLimit>`
* This filter should be configured with the name *envoy.filters.http.local_ratelimit*.

The HTTP local rate limit filter applies a :ref:`token bucket
<envoy_api_field_config.filter.http.local_rate_limit.v2.LocalRateLimit.token_bucket>` rate
limit when the request's route or virtual host has a per filter
:ref:`local rate limit configuration <envoy_api_msg_config.filter.http.local_rate_limit.v2.LocalRateLimit>`.

If the local rate limit token bucket is checked, and there are no token availables, a 429 response is returned
(the response is configurable). The local rate limit filter also sets the
:ref:`x-envoy-ratelimited<config_http_filters_router_x-envoy-ratelimited>` header. Additional response
headers may be configured.

Statistics
----------

The local rate limit filter outputs statistics in the *cluster.<route target cluster>.local_ratelimit.* namespace.
429 responses -- or the configured status code -- are emitted to the normal cluster :ref:`dynamic HTTP statistics
<config_cluster_manager_cluster_stats_dynamic_http>`.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Total under limit responses from the token bucket
  over_limit, Counter, Total responses without an available token

.. _config_http_filters_local_rate_limit_runtime:

Runtime
-------

The HTTP rate limit filter supports the following runtime settings:

local_ratelimit.<route_key>.http_filter_enabled
  % of requests that will check the local rate limit decision, but not enforce, for a given *route_key* specified
  in the :ref:`local rate limit configuration <envoy_api_msg_config.filter.http.local_rate_limit.v2.LocalRateLimit>`.
  Defaults to 100.

local_ratelimit.<route_key>.http_filter_enforcing
  % of requests that will enforce the local rate limit decision for a given *route_key* specified in the
  :ref:`local rate limit configuration <envoy_api_msg_config.filter.http.local_rate_limit.v2.LocalRateLimit>`.
  Defaults to 100. This can be used to test what would happen before fully enforcing the outcome.
